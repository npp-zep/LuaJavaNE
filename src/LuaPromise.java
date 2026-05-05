package com.luajava;

/**
 * Lua 协程 yield 出去后，Java 侧调用 complete/resumeExceptionally 唤醒协程。
 */
public class LuaPromise {
    private long statePtr;
    private int threadRef;  // 注册表中协程的引用
    private boolean done;

    public LuaPromise(long statePtr, int threadRef) {
        this.statePtr = statePtr;
        this.threadRef = threadRef;
    }

    /** 正常完成，唤醒协程并传入返回值 */
    public native void complete(Object result);

    /** 异常完成，唤醒协程并抛出错误 */
    public native void resumeExceptionally(String error);

    @Override
    protected void finalize() throws Throwable {
        if (!done) {
            resumeExceptionally("Promise was garbage collected");
        }
        super.finalize();
    }
}
