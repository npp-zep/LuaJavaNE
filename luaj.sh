#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SO_PATH="$SCRIPT_DIR/build/luajava.so"
JAR_PATH="$SCRIPT_DIR/luajava.jar"
JLINE_PATH="$SCRIPT_DIR/lib/jline.jar"

if [ -f "$SO_PATH" ]; then
    JAVA_OPTS="-Dluajava.library.path=$SO_PATH"
fi

JAVA_OPTS="$JAVA_OPTS -Dorg.jline.terminal.jna=false -Dorg.jline.terminal.jansi=false -Dorg.jline.terminal.ffm=false -Djline.native=false"

# ===== 跨平台 LuaRocks 路径检测 =====
# 尝试多种方式检测 Lua 5.4 的模块路径
detect_lua_paths() {
    # 方法1：用 pkg-config
    if command -v pkg-config &>/dev/null; then
        local LUA_LIBDIR=$(pkg-config --variable=libdir lua5.4 2>/dev/null || pkg-config --variable=libdir lua54 2>/dev/null)
        local LUA_SHAREDIR=$(pkg-config --variable=INSTALL_LMOD lua5.4 2>/dev/null || pkg-config --variable=INSTALL_LMOD lua54 2>/dev/null)
        if [ -n "$LUA_LIBDIR" ]; then
            export LUA_CPATH="$LUA_LIBDIR/lua/5.4/?.so;$LUA_LIBDIR/lua/5.4/loadall.so;;"
        fi
        if [ -n "$LUA_SHAREDIR" ]; then
            export LUA_PATH="$LUA_SHAREDIR/?.lua;$LUA_SHAREDIR/?/init.lua;;"
        fi
    fi

    # 方法2：用 lua 可执行文件检测
    if [ -z "$LUA_CPATH" ] && command -v lua5.4 &>/dev/null; then
        local LUA_ROOT=$(lua5.4 -e "print((package.cpath:gsub(';', ' ')):match('([^;]+)/%?%.so'))" 2>/dev/null)
        local LUA_SHARE=$(lua5.4 -e "print((package.path:gsub(';', ' ')):match('([^;]+)/%?%.lua'))" 2>/dev/null)
        if [ -n "$LUA_ROOT" ]; then
            export LUA_CPATH="$LUA_ROOT/?.so;$LUA_ROOT/loadall.so;;"
        fi
        if [ -n "$LUA_SHARE" ]; then
            export LUA_PATH="$LUA_SHARE/?.lua;$LUA_SHARE/?/init.lua;;"
        fi
    fi

    # 方法3：搜索常见安装位置
    if [ -z "$LUA_CPATH" ]; then
        for libdir in \
            /data/data/com.termux/files/usr/lib \
            /usr/lib /usr/lib64 /usr/local/lib \
            /opt/homebrew/lib /opt/local/lib; do
            if [ -d "$libdir/lua/5.4" ]; then
                export LUA_CPATH="$libdir/lua/5.4/?.so;$libdir/lua/5.4/loadall.so;;"
                break
            fi
        done
    fi
    if [ -z "$LUA_PATH" ]; then
        for sharedir in \
            /data/data/com.termux/files/usr/share \
            /usr/share /usr/local/share \
            /opt/homebrew/share /opt/local/share; do
            if [ -d "$sharedir/lua/5.4" ]; then
                export LUA_PATH="$sharedir/lua/5.4/?.lua;$sharedir/lua/5.4/?/init.lua;;"
                break
            fi
        done
    fi

    # 最后兜底：用通用路径
    [ -z "$LUA_CPATH" ] && export LUA_CPATH="/usr/lib/lua/5.4/?.so;/usr/local/lib/lua/5.4/?.so;;"
    [ -z "$LUA_PATH" ] && export LUA_PATH="/usr/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?.lua;;"
}

detect_lua_paths

JAVA_OPTS="$JAVA_OPTS -Dluajava.lua.cpath=$LUA_CPATH"
JAVA_OPTS="$JAVA_OPTS -Dluajava.lua.path=$LUA_PATH"
# ===== 跨平台路径检测结束 =====

# 预加载 luajava.so 以提供 Lua C API 符号给外部 .so 扩展
LD_PRELOAD="$SO_PATH" \
LD_PRELOAD="$SO_PATH:$LD_PRELOAD" java $JAVA_OPTS -cp "$JAR_PATH:$JLINE_PATH" com.luajava.LuaJMain "$@"
