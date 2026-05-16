package com.luajava;

import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

public class LuaAgent {
    private static ExecutorService executor;
    private static final ConcurrentHashMap<Integer, Object> objectRegistry = new ConcurrentHashMap<>();
    private static final AtomicInteger objectIdCounter = new AtomicInteger(1);

    public static void init() {
        if (executor == null) {
            int n = Runtime.getRuntime().availableProcessors();
            ThreadFactory daemonFactory = r -> {
                Thread t = new Thread(r);
                t.setDaemon(true);
                return t;
            };
            executor = Executors.newFixedThreadPool(Math.max(n * 2, 4), daemonFactory);
        }
    }

    public static void submitTask(AgentTask task) {
        executor.submit(() -> {
            String result;
            if (task.instance != null) {
                result = AsyncRunner.runInstance(task.instance, task.methodName, task.args);
            } else {
                result = AsyncRunner.runStatic(task.className, task.methodName, task.args);
            }
            complete(task.pid, result);
        });
    }

    /** 注册异步创建的对象，返回唯一 ID */
    public static int registerObject(Object obj) {
        int id = objectIdCounter.getAndIncrement();
        objectRegistry.put(id, obj);
        return id;
    }

    /** 根据 ID 获取对象（主线程调用） */
    public static Object getObject(int id) {
        return objectRegistry.get(id);
    }

    public static void shutdown() {
        if (executor != null) executor.shutdownNow();
    }

    private static native void complete(int pid, String result);
}
