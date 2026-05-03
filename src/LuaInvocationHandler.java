package com.luajava;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

public class LuaInvocationHandler implements InvocationHandler {
    private long statePtr;
    private int tableRef;

    public LuaInvocationHandler(long statePtr, int tableRef) {
        this.statePtr = statePtr;
        this.tableRef = tableRef;
    }

    @Override
    public native Object invoke(Object proxy, Method method, Object[] args);
}