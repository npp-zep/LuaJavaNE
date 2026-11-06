package com.luajava;

import java.lang.ref.Cleaner;

/**
 * 代表一个已编译的 Lua 函数，可在 Java 中调用。
 * 使用后应调用 {@link #destroy()} 释放本机资源；未调用时由 {@link Cleaner} 兜底。
 */
public class LuaFunctionObj {
    private static final Cleaner CLEANER = Cleaner.create();

    private long statePtr; // 指向 lua_State* 的指针
    private int ref;       // 注册表中的引用，-1 表示已释放
    private volatile boolean destroyed;
    private final Cleaner.Cleanable cleanable;

    public LuaFunctionObj(long statePtr, int ref) {
        this.statePtr = statePtr;
        this.ref = ref;
        this.destroyed = false;
        long createdState = statePtr;
        int createdRef = ref;
        // Cleaner 只捕获状态指针与注册表引用，不引用本对象
        this.cleanable = CLEANER.register(this, () -> releaseNative(createdState, createdRef));
    }

    /**
     * 调用 Lua 函数，返回所有返回值。
     * @param args 参数列表
     * @return 返回值数组（可能为 null）
     * @throws IllegalStateException 如果对象已被销毁
     * @throws RuntimeException 如果 Lua 执行出错
     */
    public Object[] callMultiple(Object... args) {
        if (destroyed) {
            throw new IllegalStateException("LuaFunctionObj has been destroyed");
        }
        return callMultipleNative(args);
    }

    /**
     * 调用 Lua 函数，返回第一个返回值（兼容旧 API）。
     * @param args 参数列表
     * @return 第一个返回值，若无则返回 null
     * @throws IllegalStateException 如果对象已被销毁
     * @throws RuntimeException 如果 Lua 执行出错
     */
    public Object call(Object... args) {
        Object[] results = callMultiple(args);
        return (results != null && results.length > 0) ? results[0] : null;
    }

    /**
     * 释放底层的 Lua 注册表引用。
     * 调用后此对象不可再用。
     * 多次调用无害。
     */
    public void destroy() {
        if (destroyed) {
            return;
        }
        synchronized (this) {
            if (destroyed) {
                return;
            }
            destroyed = true;
            cleanable.clean(); // 显式 destroy 与 GC 清理最多执行一次
            ref = -1;
            statePtr = 0;
        }
    }

    // ---------- native methods ----------
    private native Object[] callMultipleNative(Object... args);
    private static native void releaseNative(long statePtr, int ref);
}
