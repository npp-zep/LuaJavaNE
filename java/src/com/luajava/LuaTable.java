package com.luajava;

import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * 代表一个 Lua table 的惰性"活引用"。不复制数据：{@link #get}/{@link #put}
 * 在每次访问时才通过 JNI 读写底层 lua_State 的注册表引用，因此 Java 侧的改动
 * 对 Lua 侧立即可见，反之亦然。与 {@link LuaFunctionObj}（Lua 函数引用）对称。
 *
 * <p>通常由互调引擎在遇到 Lua table 参数时自动创建；也可以作为 Java 方法/回调的
 * 形参类型直接接收 Lua table。使用后应调用 {@link #destroy()} 释放本机引用。
 */
public class LuaTable {
    private static final Logger LOGGER = Logger.getLogger(LuaTable.class.getName());

    private long statePtr; // 指向 lua_State* 的指针
    private int ref;       // 注册表中的引用，-1 表示已释放
    private volatile boolean destroyed;

    public LuaTable(long statePtr, int ref) {
        this.statePtr = statePtr;
        this.ref = ref;
        this.destroyed = false;
    }

    /**
     * 读取键对应的值；键为数字/字符串/布尔等装箱类型。
     * @return Lua 值对应的 Java 对象（String/Integer/Double/Boolean/LuaTable/...），
     *         值不存在或为 nil 时返回 null
     */
    public Object get(Object key) {
        if (destroyed) {
            throw new IllegalStateException("LuaTable has been destroyed");
        }
        return getNative(key);
    }

    /**
     * 写入键值对（覆盖原值）。
     * @param value 数字/字符串/布尔等装箱类型，或任意 Java 对象（会包成 userdata）
     */
    public void put(Object key, Object value) {
        if (destroyed) {
            throw new IllegalStateException("LuaTable has been destroyed");
        }
        putNative(key, value);
    }

    /**
     * 返回 table 的长度（Lua 的 # 运算符，即 rawlen，仅精确于数组部分）。
     */
    public int size() {
        if (destroyed) {
            throw new IllegalStateException("LuaTable has been destroyed");
        }
        return sizeNative();
    }

    /**
     * 返回所有键（含任意类型，尽量还原为 Java 装箱类型）。
     */
    public Object[] keys() {
        if (destroyed) {
            throw new IllegalStateException("LuaTable has been destroyed");
        }
        return keysNative();
    }

    /**
     * 释放底层的 Lua 注册表引用；调用后此对象不可再用，多次调用无害。
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
            if (!destroyed && statePtr != 0 && ref >= 0) {
                try {
                    destroyNative();
                } catch (Throwable t) {
                    LOGGER.log(Level.WARNING, "Failed to release Lua table in finalize", t);
                }
            }
            destroyed = true;
            ref = -1;
            statePtr = 0;
        } finally {
            super.finalize();
        }
    }

    // ---------- native methods ----------
    private native Object getNative(Object key);
    private native void putNative(Object key, Object value);
    private native int sizeNative();
    private native Object[] keysNative();
    private native void destroyNative();
}