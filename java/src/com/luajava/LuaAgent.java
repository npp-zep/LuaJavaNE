package com.luajava;

import java.lang.ref.WeakReference;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.Iterator;
import java.util.Map;
import java.util.List;

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
    private static final ConcurrentHashMap<Integer, WeakReference<Object>> objectRegistry = new ConcurrentHashMap<>();
    private static final AtomicInteger objectIdCounter = new AtomicInteger(1);
    private static final int MAX_REGISTERED_OBJECTS = 10000;
    
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
        
        Callable<String> callable = () -> {
            try {
                String result;
                if (task.instance != null) {
                    result = AsyncRunner.runInstance(task.instance, task.methodName, task.args);
                } else {
                    result = AsyncRunner.runStatic(task.className, task.methodName, task.args);
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
            scheduleTimeoutCheck(pid, future, timeout, unit);
        }
        
        return future;
    }

    private static void scheduleTimeoutCheck(int pid, Future<String> future, long timeout, TimeUnit unit) {
        Thread timeoutThread = new Thread(() -> {
            try {
                Thread.sleep(unit.toMillis(timeout));
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
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        });
        timeoutThread.setDaemon(true);
        timeoutThread.start();
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

    public static Object getObject(int id) {
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

    public static boolean unregisterObject(int id) {
        WeakReference<Object> removed = objectRegistry.remove(id);
        return removed != null;
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
            "Registry stats - Total: %d, Alive: %d, Stale: %d, Max: %d",
            total, alive, stale, MAX_REGISTERED_OBJECTS
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