package com.luajava;

import java.lang.ref.WeakReference;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.Iterator;
import java.util.Map;
import java.util.List;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.logging.Logger;

public class LuaAgent {
    // ============ 状态管理 ============
    private enum State {
        RUNNING,
        SHUTTING_DOWN,
        TERMINATED
    }
    private static volatile State state = State.TERMINATED;
    
    // ============ 线程池 ============
    private static volatile ThreadPoolExecutor executor;
    
    // ============ 对象注册 ============
    private static final int MAX_REGISTERED_OBJECTS = 10000;
    private static final int MAX_PINNED_OBJECTS = 10000;

    private static final ConcurrentHashMap<Integer, WeakReference<Object>> objectRegistry = new ConcurrentHashMap<>();
    // 强引用表：用于异步任务产出的对象，保证在 getObject 取走前不会被 GC；
    // getObject 返回时即移除强引用（返回的 userdata 自行持有全局引用即可保证存活）。
    // 使用按插入顺序的 LinkedHashMap 并限制容量：若消费者始终未 getObject，
    // 超出上限时淘汰最旧的条目，避免强引用无限增长。
    private static final Map<Integer, Object> pinnedObjects =
        Collections.synchronizedMap(new LinkedHashMap<Integer, Object>(16, 0.75f, false) {
            private static final long serialVersionUID = 1L;

            @Override
            protected boolean removeEldestEntry(Map.Entry<Integer, Object> eldest) {
                if (size() <= MAX_PINNED_OBJECTS) return false;
                recordEviction(eldest.getKey());
                return true;
            }
        });
    private static final AtomicInteger objectIdCounter = new AtomicInteger(1);

    // 因超出上限被淘汰的异步结果 id（仅保留最近一批），用于区分
    // “id 不存在/已被取走”与“因对象池溢出被丢弃”，避免 getObject 静默返回 null。
    private static final int MAX_EVICTED_TRACKED = 2048;
    private static final Map<Integer, Boolean> evictedIds =
        Collections.synchronizedMap(new LinkedHashMap<Integer, Boolean>(16, 0.75f, false) {
            private static final long serialVersionUID = 1L;

            @Override
            protected boolean removeEldestEntry(Map.Entry<Integer, Boolean> eldest) {
                return size() > MAX_EVICTED_TRACKED;
            }
        });
    private static final AtomicLong totalEvictedObjects = new AtomicLong(0);
    private static final Logger LOGGER = Logger.getLogger(LuaAgent.class.getName());
    
    // ============ 同步锁 ============
    private static final Object initLock = new Object();
    
    // ============ 配置常量 ============
    private static final int CORE_MULTIPLIER = 2;
    private static final int MIN_POOL_SIZE = 4;
    private static final int SHUTDOWN_TIMEOUT_SECONDS = 30;
    private static final int FORCE_SHUTDOWN_TIMEOUT_SECONDS = 5;
    private static final long KEEP_ALIVE_TIME = 60L;
    private static final long DEFAULT_TASK_TIMEOUT_SECONDS = 300L;
    
    // ============ 统计信息 ============
    private static final AtomicLong totalTasksSubmitted = new AtomicLong(0);
    private static final AtomicLong totalTasksCompleted = new AtomicLong(0);
    private static final AtomicLong totalTasksFailed = new AtomicLong(0);
    private static final AtomicLong totalTasksTimeout = new AtomicLong(0);
    private static final AtomicLong totalTasksCancelled = new AtomicLong(0);
    
    // ============ 任务跟踪 ============
    private static final ConcurrentHashMap<Integer, Future<?>> taskFutures = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Integer, Long> taskStartTimes = new ConcurrentHashMap<>();
    // 单个调度线程负责所有任务超时，避免每个任务创建一个 Thread
    private static volatile ScheduledThreadPoolExecutor timeoutScheduler;

    // ============================================================
    // 初始化
    // ============================================================

    public static void init() {
        if (state == State.RUNNING && executor != null) {
            return;
        }
        
        synchronized (initLock) {
            if (state == State.RUNNING && executor != null) {
                return;
            }
            
            if (state == State.SHUTTING_DOWN) {
                throw new IllegalStateException(
                    "LuaAgent is currently shutting down, cannot re-initialize"
                );
            }
            
            if (state == State.TERMINATED) {
                if (executor != null) {
                    executor.shutdownNow();
                    executor = null;
                }
                shutdownTimeoutScheduler();
                resetStats();
            }
            
            int n = Runtime.getRuntime().availableProcessors();
            int corePoolSize = Math.max(n * CORE_MULTIPLIER, MIN_POOL_SIZE);
            ThreadFactory daemonFactory = r -> {
                Thread t = new Thread(r);
                t.setDaemon(true);
                t.setUncaughtExceptionHandler((thread, e) -> {});
                return t;
            };
            executor = new ThreadPoolExecutor(
                corePoolSize,
                corePoolSize,
                KEEP_ALIVE_TIME,
                TimeUnit.SECONDS,
                new LinkedBlockingQueue<>(),
                daemonFactory,
                new ThreadPoolExecutor.AbortPolicy()
            );
            timeoutScheduler = new ScheduledThreadPoolExecutor(1, daemonFactory);
            // 任务完成时取消对应的延迟任务，避免已完成的超时检查堆积到超时时间点
            timeoutScheduler.setRemoveOnCancelPolicy(true);
            state = State.RUNNING;
        }
    }

    // ============================================================
    // 任务提交方法
    // ============================================================

    public static void submitTask(AgentTask task) throws RejectedExecutionException {
        submitTaskInternal(task, 0, null);
    }

    public static Future<String> submitTaskFuture(AgentTask task) throws RejectedExecutionException {
        return submitTaskFuture(task, DEFAULT_TASK_TIMEOUT_SECONDS, TimeUnit.SECONDS);
    }

    public static Future<String> submitTaskFuture(AgentTask task, long timeout, TimeUnit unit) 
            throws RejectedExecutionException {
        return submitTaskInternal(task, timeout, unit);
    }

    @SuppressWarnings("unchecked")
    private static Future<String> submitTaskInternal(AgentTask task, long timeout, TimeUnit unit) 
            throws RejectedExecutionException {
        if (state != State.RUNNING) {
            throw new RejectedExecutionException(
                "LuaAgent is not running (state: " + state + ")"
            );
        }
        
        init();
        
        if (state != State.RUNNING) {
            throw new RejectedExecutionException(
                "LuaAgent state changed during initialization: " + state
            );
        }
        
        totalTasksSubmitted.incrementAndGet();
        final int pid = task.pid;
        taskStartTimes.put(pid, System.currentTimeMillis());
        final AtomicReference<ScheduledFuture<?>> timeoutTask = new AtomicReference<>();
        
        Callable<String> callable = () -> {
            try {
                String result;
                if (task.instance != null) {
                    result = AsyncRunner.runInstance(task.instance, task.methodName, task.args, task.expandArrays);
                } else {
                    result = AsyncRunner.runStatic(task.className, task.methodName, task.args, task.expandArrays);
                }
                
                totalTasksCompleted.incrementAndGet();
                taskStartTimes.remove(pid);
                complete(pid, result);
                return result;
                
            } catch (Exception e) {
                totalTasksFailed.incrementAndGet();
                taskStartTimes.remove(pid);
                String errorMsg = "E:" + e.toString();
                try {
                    complete(pid, errorMsg);
                } catch (Exception completeEx) {
                    // ignore
                }
                throw e;
            } finally {
                // 任务结束（成功/失败/被超时取消）后取消延迟检查，避免其堆积到超时时间点
                ScheduledFuture<?> scheduled = timeoutTask.get();
                if (scheduled != null) scheduled.cancel(false);
            }
        };
        
        Future<String> future;
        try {
            future = executor.submit(callable);
        } catch (RejectedExecutionException e) {
            totalTasksFailed.incrementAndGet();
            taskStartTimes.remove(pid);
            throw e;
        } catch (Exception e) {
            totalTasksFailed.incrementAndGet();
            taskStartTimes.remove(pid);
            throw new RuntimeException("Failed to submit task", e);
        }
        
        taskFutures.put(pid, future);
        
        if (timeout > 0 && unit != null) {
            ScheduledFuture<?> scheduled = scheduleTimeoutCheck(pid, future, timeout, unit);
            timeoutTask.set(scheduled);
            // 任务可能在延迟检查入队前就已结束
            if (scheduled != null && future.isDone()) scheduled.cancel(false);
        }
        
        return future;
    }

    private static ScheduledFuture<?> scheduleTimeoutCheck(int pid, Future<String> future,
                                                          long timeout, TimeUnit unit) {
        ScheduledThreadPoolExecutor scheduler = timeoutScheduler;
        if (scheduler == null || scheduler.isShutdown()) return null;
        try {
            return scheduler.schedule(() -> {
                if (!future.isDone() && !future.isCancelled()) {
                    boolean cancelled = future.cancel(true);
                    if (cancelled) {
                        totalTasksTimeout.incrementAndGet();
                        taskFutures.remove(pid);
                        taskStartTimes.remove(pid);
                        try {
                            complete(pid, "E:Task timeout after " + timeout + " " + unit);
                        } catch (Exception e) {
                            // ignore
                        }
                    }
                }
            }, timeout, unit);
        } catch (RejectedExecutionException e) {
            return null;
        }
    }

    private static void shutdownTimeoutScheduler() {
        ScheduledThreadPoolExecutor scheduler = timeoutScheduler;
        timeoutScheduler = null;
        if (scheduler != null) {
            scheduler.shutdownNow();
        }
    }

    // ============================================================
    // 任务控制方法
    // ============================================================

    public static boolean cancelTask(int pid, boolean mayInterruptIfRunning) {
        Future<?> future = taskFutures.remove(pid);
        if (future != null && !future.isDone()) {
            boolean cancelled = future.cancel(mayInterruptIfRunning);
            if (cancelled) {
                totalTasksCancelled.incrementAndGet();
                taskStartTimes.remove(pid);
                return true;
            }
        }
        return false;
    }

    @SuppressWarnings("unchecked")
    public static String awaitTask(int pid, long timeout, TimeUnit unit) 
            throws TimeoutException, ExecutionException, InterruptedException {
        Future<?> future = taskFutures.get(pid);
        if (future == null) {
            throw new IllegalArgumentException("No such task: " + pid);
        }
        try {
            return (String) future.get(timeout, unit);
        } catch (TimeoutException e) {
            future.cancel(true);
            taskFutures.remove(pid);
            taskStartTimes.remove(pid);
            throw e;
        }
    }

    // ============================================================
    // 统计信息接口
    // ============================================================

    public static String getTaskStats() {
        return String.format(
            "Task stats - Submitted: %d, Completed: %d, Failed: %d, Timeout: %d, Cancelled: %d, Running: %d",
            totalTasksSubmitted.get(),
            totalTasksCompleted.get(),
            totalTasksFailed.get(),
            totalTasksTimeout.get(),
            totalTasksCancelled.get(),
            getRunningTaskCount()
        );
    }

    public static int getRunningTaskCount() {
        if (executor != null) {
            return executor.getActiveCount();
        }
        return 0;
    }

    public static String getExecutorStats() {
        if (executor == null) {
            return "Executor not initialized";
        }
        return String.format(
            "Executor stats - Pool size: %d, Active: %d, Queue: %d, Completed tasks: %d",
            executor.getPoolSize(),
            executor.getActiveCount(),
            executor.getQueue().size(),
            executor.getCompletedTaskCount()
        );
    }

    public static void resetStats() {
        totalTasksSubmitted.set(0);
        totalTasksCompleted.set(0);
        totalTasksFailed.set(0);
        totalTasksTimeout.set(0);
        totalTasksCancelled.set(0);
        totalEvictedObjects.set(0);
        evictedIds.clear();
        taskFutures.clear();
        taskStartTimes.clear();
    }

    // ============================================================
    // 对象注册
    // ============================================================

    public static int registerObject(Object obj) {
        cleanupStaleReferences();
        
        int currentSize = objectRegistry.size();
        if (currentSize >= MAX_REGISTERED_OBJECTS) {
            throw new IllegalStateException(
                "Object registry exceeded maximum size: " + MAX_REGISTERED_OBJECTS
            );
        }
        
        int id = objectIdCounter.getAndIncrement();
        objectRegistry.put(id, new WeakReference<>(obj));
        return id;
    }

    // 异步结果对象：以强引用登记，保证在 getObject 前不被 GC。
    // 只登记到 pinnedObjects：被淘汰（未及时 getObject）后对象即可回收，
    // 不再额外写弱引用表，避免弱引用条目随任务量无界增长。
    public static int registerObjectStrong(Object obj) {
        int id = objectIdCounter.getAndIncrement();
        pinnedObjects.put(id, obj);
        return id;
    }

    // 异步调用参数中的 Java 对象：单独一张表，非破坏性读取（重载匹配可能多次访问），
    // AsyncRunner 在一次任务结束后统一释放。
    private static final Map<Integer, Object> argumentObjects = new ConcurrentHashMap<>();
    private static final AtomicInteger argumentIdCounter = new AtomicInteger(1);

    public static int registerArgumentObject(Object obj) {
        int id = argumentIdCounter.getAndIncrement();
        argumentObjects.put(id, obj);
        return id;
    }

    public static Object peekArgumentObject(int id) {
        return argumentObjects.get(id);
    }

    public static void releaseArgumentObject(int id) {
        argumentObjects.remove(id);
    }

    public static Object getObject(int id) {
        // 强引用(异步)对象：取走即释放，返回的 userdata 自行持有全局引用保证存活
        Object pinned = pinnedObjects.remove(id);
        if (pinned != null) {
            objectRegistry.remove(id);
            return pinned;
        }
        WeakReference<Object> ref = objectRegistry.get(id);
        if (ref == null) {
            return null;
        }
        Object obj = ref.get();
        if (obj == null) {
            objectRegistry.remove(id);
        }
        return obj;
    }

    // 该 id 是否因异步结果池溢出而被淘汰（区别于“不存在/已被取走”）
    public static boolean wasEvicted(int id) {
        return evictedIds.containsKey(id);
    }

    // 淘汰时记录 id 并告警（首次与每 1000 次打印一条，避免刷屏）
    private static void recordEviction(int id) {
        evictedIds.put(id, Boolean.TRUE);
        long total = totalEvictedObjects.incrementAndGet();
        if (total == 1 || total % 1000 == 0) {
            LOGGER.warning("Async result pool exceeded " + MAX_PINNED_OBJECTS
                    + " entries; discarded result id " + id
                    + " (total evicted: " + total + ")");
        }
    }

    public static long getEvictedObjectCount() {
        return totalEvictedObjects.get();
    }

    // 释放一个尚未被 getObject 取走的异步对象强引用（防泄漏）
    public static boolean unregisterStrongObject(int id) {
        return pinnedObjects.remove(id) != null;
    }

    public static boolean unregisterObject(int id) {
        // 同时释放可能存在的强引用（异步结果），避免只登记不取走造成泄漏
        boolean pinnedRemoved = pinnedObjects.remove(id) != null;
        WeakReference<Object> removed = objectRegistry.remove(id);
        return removed != null || pinnedRemoved;
    }

    public static int getRegistrySize() {
        cleanupStaleReferences();
        return objectRegistry.size();
    }

    public static void cleanupStaleReferences() {
        Iterator<Map.Entry<Integer, WeakReference<Object>>> iterator = 
            objectRegistry.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Integer, WeakReference<Object>> entry = iterator.next();
            if (entry.getValue().get() == null) {
                iterator.remove();
            }
        }
    }

    public static void clearRegistry() {
        objectRegistry.clear();
        pinnedObjects.clear();
        evictedIds.clear();
    }

    public static String getRegistryStats() {
        int total = objectRegistry.size();
        int alive = 0;
        int stale = 0;
        
        for (WeakReference<Object> ref : objectRegistry.values()) {
            if (ref.get() != null) {
                alive++;
            } else {
                stale++;
            }
        }
        
        return String.format(
            "Registry stats - Total: %d, Alive: %d, Stale: %d, Max: %d, Pinned: %d, Evicted: %d",
            total, alive, stale, MAX_REGISTERED_OBJECTS, pinnedObjects.size(),
            totalEvictedObjects.get()
        );
    }

    // ============================================================
    // 生命周期管理
    // ============================================================

    public static void shutdown() {
        synchronized (initLock) {
            if (state == State.TERMINATED) {
                return;
            }
            
            if (state == State.SHUTTING_DOWN) {
                return;
            }
            
            state = State.SHUTTING_DOWN;
            
            if (executor == null) {
                state = State.TERMINATED;
                shutdownTimeoutScheduler();
                return;
            }
            
            executor.shutdown();
            try {
                if (!executor.awaitTermination(SHUTDOWN_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                    List<Runnable> remaining = executor.shutdownNow();
                    if (!executor.awaitTermination(FORCE_SHUTDOWN_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                        // ignore
                    }
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                executor.shutdownNow();
            } finally {
                executor = null;
                state = State.TERMINATED;
                shutdownTimeoutScheduler();
                clearRegistry();
                taskFutures.clear();
                taskStartTimes.clear();
            }
        }
    }

    public static void shutdownNow() {
        synchronized (initLock) {
            if (state == State.TERMINATED) {
                return;
            }
            
            ThreadPoolExecutor oldExecutor = executor;
            state = State.SHUTTING_DOWN;
            
            if (oldExecutor != null) {
                oldExecutor.shutdownNow();
            }
            
            executor = null;
            state = State.TERMINATED;
            shutdownTimeoutScheduler();
            clearRegistry();
            taskFutures.clear();
            taskStartTimes.clear();
        }
    }

    public static boolean isShutdown() {
        return state == State.SHUTTING_DOWN || state == State.TERMINATED;
    }

    public static boolean isTerminated() {
        if (state == State.TERMINATED) {
            return true;
        }
        if (state == State.SHUTTING_DOWN && executor != null) {
            return executor.isTerminated();
        }
        return false;
    }

    public static String getState() {
        return state.name();
    }

    // ============================================================
    // JNI 回调
    // ============================================================

    private static native void complete(int pid, String result);
}
