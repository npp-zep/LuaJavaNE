package com.luajava.exception;

/**
 * 已注册的 Java 方法被调用时抛出的异常（Lua 调 Java）。
 *
 * <p>用于把 checked 异常统一包装为运行时异常，同时保留原始根因；
 * 若被调方法本身抛出 {@link RuntimeException} 或 {@link Error}，则原样向上传播而不包装。</p>
 */
public class JavaInvocationError extends LuaJavaException {

    public JavaInvocationError(String message) {
        super(message);
    }

    public JavaInvocationError(String message, Throwable cause) {
        super(message, cause);
    }
}