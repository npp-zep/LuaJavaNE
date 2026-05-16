package com.luajava;

import java.util.concurrent.*;

public class LuaAgent {
    private static ExecutorService executor;

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

    public static void shutdown() {
        if (executor != null) executor.shutdownNow();
    }

    private static native void complete(int pid, String result);
}
