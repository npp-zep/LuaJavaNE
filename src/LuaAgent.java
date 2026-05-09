package com.luajava;

import java.util.concurrent.*;

public class LuaAgent {
    private ExecutorService executor;
    private ConcurrentLinkedQueue<Runnable> pendingTasks;

    public LuaAgent() {
        this.executor = Executors.newSingleThreadExecutor();
        this.pendingTasks = new ConcurrentLinkedQueue<>();
    }

    /** 工作线程调用：在主线程的 lua_State 上执行函数引用 */
    public void submitTask(int promiseId, long funcRef) {
        pendingTasks.add(() -> {
            // 由主线程的 poll() 调用，在主线程中执行
            LuaRuntime.executeAsync(promiseId, funcRef);
        });
    }

    /** 主线程轮询：取出所有待执行的 Lua 任务并执行 */
    public void poll(LuaRuntime L) {
        Runnable task;
        while ((task = pendingTasks.poll()) != null) {
            task.run();
        }
    }

    public void shutdown() {
        executor.shutdown();
    }
}
