package com.luajava.exception;

/**
 * LuaJavaNE 异常层次结构的基类。
 *
 * <p>所有具体异常均继承自 {@link java.lang.RuntimeException}，以保证与既有
 * {@code catch (RuntimeException)} / {@code assertThrows(RuntimeException)} 用法
 * 向后兼容；同时通过子类区分不同错误来源，便于精确捕获与定位。</p>
 */
public abstract class LuaJavaException extends RuntimeException {

    public LuaJavaException(String message) {
        super(message);
    }

    public LuaJavaException(String message, Throwable cause) {
        super(message, cause);
    }

    public LuaJavaException(Throwable cause) {
        super(cause);
    }
}