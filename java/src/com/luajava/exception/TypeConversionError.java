package com.luajava.exception;

/**
 * Lua 与 Java 之间的参数类型转换失败。
 *
 * <p>例如异步任务将字符串参数按目标参数类型转换（{@code parseInt} 等）失败时抛出，
 * 用于在方法匹配失败时给出可读的诊断信息。</p>
 */
public class TypeConversionError extends LuaJavaException {

    public TypeConversionError(String message) {
        super(message);
    }

    public TypeConversionError(String message, Throwable cause) {
        super(message, cause);
    }
}