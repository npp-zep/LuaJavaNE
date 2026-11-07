package com.luajava;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.ref.Cleaner;
import java.util.logging.Level;
import java.util.logging.Logger;

public class LuaInvocationHandler implements InvocationHandler {
    private static final Logger LOGGER = Logger.getLogger(LuaInvocationHandler.class.getName());
    private static final Cleaner cleaner = Cleaner.create();

    private long statePtr;
    private int tableRef;
    private volatile boolean destroyed;
    private final Cleaner.Cleanable cleanable;

    private static class CleanupTask implements Runnable {
        private final long statePtr;
        private final int tableRef;
        
        CleanupTask(long statePtr, int tableRef) {
            this.statePtr = statePtr;
            this.tableRef = tableRef;
        }
        
        @Override
        public void run() {
            if (statePtr != 0 && tableRef >= 0) {
                try {
                    destroyNative(statePtr, tableRef);
                } catch (Throwable t) {
                    LOGGER.log(Level.WARNING, "Failed to release Lua table reference in Cleaner", t);
                }
            }
        }
    }

    public LuaInvocationHandler(long statePtr, int tableRef) {
        this.statePtr = statePtr;
        this.tableRef = tableRef;
        this.destroyed = false;
        this.cleanable = cleaner.register(this, new CleanupTask(statePtr, tableRef));
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        if (destroyed) {
            throw new IllegalStateException("LuaInvocationHandler has been destroyed");
        }
        try {
            return invokeNative(proxy, method, args);
        } catch (NoSuchMethodError e) {
            // handler 表未定义该方法时，接口 default 方法走 Java 默认实现
            if (method.isDefault()) {
                return InvocationHandler.invokeDefault(proxy, method, args == null ? new Object[0] : args);
            }
            // Object 方法按代理自身语义处理，避免代理无法用于集合、日志、Objects.equals 等场景
            if (method.getDeclaringClass() == Object.class) {
                switch (method.getName()) {
                    case "toString":
                        return "LuaProxy@" + Integer.toHexString(System.identityHashCode(proxy));
                    case "hashCode":
                        return System.identityHashCode(proxy);
                    case "equals":
                        return proxy == (args == null ? null : args[0]);
                    default:
                        break;
                }
            }
            throw e;
        }
    }

    public void destroy() {
        if (destroyed) {
            return;
        }
        synchronized (this) {
            if (destroyed) {
                return;
            }
            // 与 Cleaner 共用同一个 Cleanable：显式 destroy 与 GC 清理最多执行一次
            cleanable.clean();
            tableRef = -1;
            destroyed = true;
            statePtr = 0;
        }
    }

    // ---------- native methods ----------
    private native Object invokeNative(Object proxy, Method method, Object[] args);
    private static native void destroyNative(long statePtr, int tableRef);
}