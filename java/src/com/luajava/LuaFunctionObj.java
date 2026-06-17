package com.luajava;

public class LuaFunctionObj {
    private long statePtr;
    private int ref;

    public LuaFunctionObj(long statePtr, int ref) {
        this.statePtr = statePtr;
        this.ref = ref;
    }

    /** 调用函数，返回第一个返回值（兼容旧 API） */
    public Object call(Object... args) {
        Object[] results = callMultiple(args);
        return (results != null && results.length > 0) ? results[0] : null;
    }

    /** 调用函数，返回所有返回值 */
    public native Object[] callMultiple(Object... args);

    /** 释放引用 */
    public native void destroy();

    @Override
    protected void finalize() throws Throwable {
        destroy();
        super.finalize();
    }
}