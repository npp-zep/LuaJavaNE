package com.luajava;

import java.util.concurrent.*;

public class LuaAgent {
    private ExecutorService executor;

    public LuaAgent() {
        this.executor = Executors.newCachedThreadPool();
    }

    public void submitTask(Runnable task) {
        executor.submit(task);
    }

    public void poll(LuaRuntime L) {}

    public void shutdown() {
        executor.shutdown();
    }
}
