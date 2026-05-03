package com.luajava;

import java.lang.reflect.Method;

public class LuaJavaCallback {
    private Object module;
    private Method method;

    public LuaJavaCallback(Object module, Method method) {
        this.module = module;
        this.method = method;
    }

    public Object call(Object... args) {
        try {
            return method.invoke(module, args);
        } catch (Exception e) {
            throw new RuntimeException("Error invoking " + method.getName(), e);
        }
    }
}
