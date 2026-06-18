package com.luajava;

import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * 代表一个已编译的 Lua 函数，可在 Java 中调用。
 * 使用后必须调用 {@link #destroy()} 释放本机资源，或依赖 finalize（不推荐）。
 */
public class LuaFunctionObj {
    private static final Logger LOGGER = Logger.getLogger(LuaFunctionObj.class.getName());

    private long statePtr; // 指向 lua_State* 的指针
    private int ref;       // 注册表中的引用，-1 表示已释放
    private volatile boolean destroyed;

    public LuaFunctionObj(long statePtr, int ref) {
        this.statePtr = statePtr;
        this.ref = ref;
        this.destroyed = false;
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
            if (statePtr != 0 && ref >= 0) {
                destroyNative();
                ref = -1;
            }
            destroyed = true;
            statePtr = 0;
        }
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (!destroyed) {
                // 如果未被显式销毁，尝试释放
                if (statePtr != 0 && ref >= 0) {
                    // 注意：finalize 中可能无法保证 statePtr 仍然有效，因此捕获异常
                    try {
                        destroyNative();
                    } catch (Throwable t) {
                        LOGGER.log(Level.WARNING, "Failed to release Lua function in finalize", t);
                    }
                }
                destroyed = true;
                ref = -1;
                statePtr = 0;
            }
        } finally {
            super.finalize();
        }
    }

    // ---------- native methods ----------
    private native Object[] callMultipleNative(Object... args);
    private native void destroyNative();
}