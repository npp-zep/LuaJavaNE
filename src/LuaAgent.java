package com.luajava;

public class LuaAgent {
    private static LuaAgent instance;

    public static void init() {
        if (instance == null) instance = new LuaAgent();
    }

    public static void submitTask(AgentTask task) {
        new Thread(() -> {
            String result;
            if (task.instance != null) {
                result = AsyncRunner.runInstance(task.instance, task.methodName, task.args);
            } else {
                result = AsyncRunner.runStatic(task.className, task.methodName, task.args);
            }
            complete(task.pid, result);
        }).start();
    }

    private static native void complete(int pid, String result);
}
