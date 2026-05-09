package com.luajava;

public class LuaAgent {
    private static LuaAgent instance;

    public static void init() {
        if (instance == null) instance = new LuaAgent();
    }

    public static void submitTask(AgentTask task) {
        new Thread(() -> {
            try {
                String result = AsyncRunner.runStatic(
                    task.className, task.methodName, task.args);
                complete(task.pid, result);
            } catch (Exception e) {
                complete(task.pid, "E:" + e.getMessage());
            }
        }).start();
    }

    private static native void complete(int pid, String result);
}
