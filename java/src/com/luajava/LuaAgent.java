package com.luajava;

import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Level;
import java.util.logging.Logger;

public class LuaAgent {
    private static final Logger LOGGER = Logger.getLogger(LuaAgent.class.getName());
    private static volatile ExecutorService executor;
    private static final ConcurrentHashMap<Integer, Object> objectRegistry = new ConcurrentHashMap<>();
    private static final AtomicInteger objectIdCounter = new AtomicInteger(1);
    private static final AtomicBoolean initialized = new AtomicBoolean(false);
    private static final Object initLock = new Object();

    /**
     * 初始化线程池（线程安全，仅执行一次）。
     * 若已初始化则直接返回。
     */
    public static void init() {
        if (initialized.get()) {
            return;
        }
        synchronized (initLock) {
            if (initialized.get()) {
                return;
            }
            int n = Runtime.getRuntime().availableProcessors();
            ThreadFactory daemonFactory = r -> {
                Thread t = new Thread(r);
                t.setDaemon(true);
                t.setUncaughtExceptionHandler((thread, e) ->
                    LOGGER.log(Level.SEVERE, "Uncaught exception in agent thread", e));
                return t;
            };
            executor = Executors.newFixedThreadPool(Math.max(n * 2, 4), daemonFactory);
            initialized.set(true);
        }
    }

    /**
     * 提交任务到线程池。
     * 如果线程池未初始化，自动初始化。
     */
    public static void submitTask(AgentTask task) {
        init();
        executor.submit(() -> {
            try {
                String result;
                if (task.instance != null) {
                    result = AsyncRunner.runInstance(task.instance, task.methodName, task.args);
                } else {
                    result = AsyncRunner.runStatic(task.className, task.methodName, task.args);
                }
                complete(task.pid, result);
            } catch (Exception e) {
                String errorMsg = "E:" + e.toString();
                try {
                    complete(task.pid, errorMsg);
                } catch (Exception ex) {
                    LOGGER.log(Level.SEVERE, "Failed to send completion error for task " + task.pid, ex);
                }
                LOGGER.log(Level.WARNING, "Task " + task.pid + " failed", e);
            }
        });
    }

    /**
     * 注册 Java 对象，返回唯一 ID。
     */
    public static int registerObject(Object obj) {
        int id = objectIdCounter.getAndIncrement();
        objectRegistry.put(id, obj);
        return id;
    }

    /**
     * 根据 ID 获取注册的对象。
     */
    public static Object getObject(int id) {
        return objectRegistry.get(id);
    }

    /**
     * 优雅关闭线程池（等待所有任务完成）。
     * 调用后，无法再提交新任务（但可再次调用 init 重新初始化）。
     */
    public static void shutdown() {
        if (!initialized.get()) {
            return;
        }
        synchronized (initLock) {
            if (!initialized.get() || executor == null) {
                return;
            }
            executor.shutdown();
            try {
                if (!executor.awaitTermination(30, TimeUnit.SECONDS)) {
                    executor.shutdownNow();
                    if (!executor.awaitTermination(5, TimeUnit.SECONDS)) {
                        LOGGER.warning("LuaAgent thread pool did not terminate");
                    }
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                executor.shutdownNow();
                LOGGER.log(Level.WARNING, "Shutdown interrupted, forcing termination", e);
            }
            executor = null;
            initialized.set(false);
        }
    }

    /**
     * 立即强制关闭线程池（不等任务完成）。
     */
    public static void shutdownNow() {
        if (!initialized.get()) {
            return;
        }
        synchronized (initLock) {
            if (!initialized.get() || executor == null) {
                return;
            }
            executor.shutdownNow();
            executor = null;
            initialized.set(false);
        }
    }

    /**
     * 检查线程池是否已关闭。
     */
    public static boolean isShutdown() {
        return !initialized.get() || executor == null || executor.isShutdown();
    }

    /**
     * 检查线程池是否已终止（所有任务完成）。
     */
    public static boolean isTerminated() {
        return executor != null && executor.isTerminated();
    }

    /**
     * JNI 回调：通知 Lua 侧任务完成。
     */
    private static native void complete(int pid, String result);
}