package com.luajava;

import java.util.concurrent.*;

/**
 * 后台线程代理，负责跨线程任务调度和结果通知。
 * 主线程通过 poll() 获取已完成的结果。
 */
public class LuaAgent {
    private ExecutorService executor;
    private ConcurrentLinkedQueue<Result> doneQueue;

    public LuaAgent() {
        this.executor = Executors.newCachedThreadPool();
        this.doneQueue = new ConcurrentLinkedQueue<>();
    }

    /** 提交异步任务：执行 Lua 函数，完成后将 Promise ID 和结果放入完成队列 */
    public void submitTask(LuaRuntime L, int promiseId, int funcRef) {
        executor.submit(() -> {
            try {
                L.callFunction("__agent_exec", funcRef);
                doneQueue.add(new Result(promiseId, null, null));
            } catch (Exception e) {
                doneQueue.add(new Result(promiseId, null, e.getMessage()));
            }
        });
    }

    /** 主线程轮询：取出所有已完成的任务并 complete 对应的 promise */
    public void poll(LuaRuntime L) {
        Result r;
        while ((r = doneQueue.poll()) != null) {
            if (r.error != null) {
                L.doString("java.complete(" + r.promiseId + ", nil, '" + r.error + "')");
            } else {
                L.doString("java.complete(" + r.promiseId + ", 'done')");
            }
        }
    }

    /** 关闭线程池 */
    public void shutdown() {
        executor.shutdown();
    }

    private static class Result {
        int promiseId;
        Object value;
        String error;

        Result(int id, Object val, String err) {
            this.promiseId = id;
            this.value = val;
            this.error = err;
        }
    }
}
