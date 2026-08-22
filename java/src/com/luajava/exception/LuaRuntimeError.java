package com.luajava.exception;

/**
 * Lua 执行期错误（{@code lua_pcall} 未捕获的错误）。
 *
 * <p>例如 {@code doString} / {@code doFile} / {@code callFunction} 执行时
 * 脚本内部调用 {@code error()} 或抛出运行时错误。</p>
 *
 * <p>注意：语法错误（编译期）由子类 {@link LuaSyntaxError} 表示。</p>
 */
public class LuaRuntimeError extends LuaJavaException {

    public LuaRuntimeError(String message) {
        super(message);
    }

    public LuaRuntimeError(String message, Throwable cause) {
        super(message, cause);
    }
}