package com.luajava;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * 包装 Java 方法，使其可从 Lua 调用。
 * 当方法抛出异常时，将原始异常重新抛出或包装，保留完整堆栈。
 */
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
        } catch (InvocationTargetException e) {
            // 提取目标异常的原因
            Throwable cause = e.getCause();
            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            } else if (cause instanceof Error) {
                throw (Error) cause;
            } else {
                // 包装为 RuntimeException 并保留原因
                throw new RuntimeException("Error invoking " + method.getName() + ": " + cause.getMessage(), cause);
            }
        } catch (IllegalAccessException e) {
            // 访问权限错误
            throw new RuntimeException("Illegal access invoking " + method.getName(), e);
        } catch (Exception e) {
            // 其他异常（理论上不会发生）
            throw new RuntimeException("Unexpected error invoking " + method.getName(), e);
        }
    }
}