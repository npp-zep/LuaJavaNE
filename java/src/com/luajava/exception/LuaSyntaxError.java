package com.luajava.exception;

/**
 * Lua 编译期错误（{@code luaL_load*} 阶段，对应 C 层 {@code LUA_ERRSYNTAX}）。
 *
 * <p>例如代码存在语法错误时由 {@code LuaRuntime.compile()} 抛出。</p>
 */
public class LuaSyntaxError extends LuaRuntimeError {

    public LuaSyntaxError(String message) {
        super(message);
    }

    public LuaSyntaxError(String message, Throwable cause) {
        super(message, cause);
    }
}