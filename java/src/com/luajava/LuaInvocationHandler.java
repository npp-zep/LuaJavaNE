package com.luajava;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * 动态代理处理器：将 Java 接口调用转发到 Lua 表。
 * 使用后必须调用 {@link #destroy()} 释放本机资源，或依赖 finalize（不推荐）。
 */
public class LuaInvocationHandler implements InvocationHandler {
    private static final Logger LOGGER = Logger.getLogger(LuaInvocationHandler.class.getName());

    private long statePtr;      // 指向 lua_State* 的指针
    private int tableRef;       // 注册表中的 Lua 表引用，-1 表示已释放
    private volatile boolean destroyed;

    public LuaInvocationHandler(long statePtr, int tableRef) {
        this.statePtr = statePtr;
        this.tableRef = tableRef;
        this.destroyed = false;
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        if (destroyed) {
            throw new IllegalStateException("LuaInvocationHandler has been destroyed");
        }
        return invokeNative(proxy, method, args);
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
            if (statePtr != 0 && tableRef >= 0) {
                destroyNative();
                tableRef = -1;
            }
            destroyed = true;
            statePtr = 0;
        }
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (!destroyed) {
                if (statePtr != 0 && tableRef >= 0) {
                    try {
                        destroyNative();
                    } catch (Throwable t) {
                        LOGGER.log(Level.WARNING, "Failed to release Lua table reference in finalize", t);
                    }
                }
                destroyed = true;
                tableRef = -1;
                statePtr = 0;
            }
        } finally {
            super.finalize();
        }
    }

    // ---------- native methods ----------
    private native Object invokeNative(Object proxy, Method method, Object[] args);
    private native void destroyNative();
}