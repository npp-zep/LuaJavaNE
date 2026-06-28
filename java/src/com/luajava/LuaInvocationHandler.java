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
        cleaner.register(this, new CleanupTask(statePtr, tableRef));
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        if (destroyed) {
            throw new IllegalStateException("LuaInvocationHandler has been destroyed");
        }
        return invokeNative(proxy, method, args);
    }

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

    // ---------- native methods ----------
    private native Object invokeNative(Object proxy, Method method, Object[] args);
    private native void destroyNative();
    private static native void destroyNative(long statePtr, int tableRef);
}