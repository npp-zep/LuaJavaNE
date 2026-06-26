#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ===== 读取版本配置 =====
if [ -f "$SCRIPT_DIR/version.properties" ]; then
    . "$SCRIPT_DIR/version.properties"
fi
MIN_JDK="${MIN_JDK_VERSION:-17}"
REC_JDK="${RECOMMENDED_JDK_VERSION:-21}"


# ===== 优先使用当前目录的 .so =====
if [ -f "$SCRIPT_DIR/luajava.so" ]; then
    SO_PATH="$SCRIPT_DIR/luajava.so"
elif [ -f "$SCRIPT_DIR/build/luajava.so" ]; then
    SO_PATH="$SCRIPT_DIR/build/luajava.so"
else
    SO_PATH=""
fi

JAR_PATH="$SCRIPT_DIR/luajava.jar"
JLINE_PATH="$SCRIPT_DIR/lib/jline.jar"

JAVA_OPTS=""
if [ -f "$SO_PATH" ]; then
    JAVA_OPTS="-Dluajava.library.path=$SO_PATH"
fi
JAVA_OPTS="$JAVA_OPTS -Dorg.jline.terminal.jna=false -Dorg.jline.terminal.jansi=false -Dorg.jline.terminal.ffm=false -Djline.native=false"

# ===== 传递版本信息到 Java =====
if [ -f "$SCRIPT_DIR/version.properties" ]; then
    JAVA_OPTS="$JAVA_OPTS -Dluajava.version=$PROJECT_VERSION"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.name=$PROJECT_NAME"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.copyright=$PROJECT_COPYRIGHT"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.license=$PROJECT_LICENSE"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.url=$PROJECT_URL"
fi

# ===== 跨平台 LuaRocks 路径检测 =====
detect_lua_paths() {
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
    if [ -z "$LUA_CPATH" ]; then
        for libdir in /data/data/com.termux/files/usr/lib /usr/lib /usr/lib64 /usr/local/lib /opt/homebrew/lib /opt/local/lib; do
            if [ -d "$libdir/lua/5.4" ]; then
                export LUA_CPATH="$libdir/lua/5.4/?.so;$libdir/lua/5.4/loadall.so;;"
                break
            fi
        done
    fi
    if [ -z "$LUA_PATH" ]; then
        for sharedir in /data/data/com.termux/files/usr/share /usr/share /usr/local/share /opt/homebrew/share /opt/local/share; do
            if [ -d "$sharedir/lua/5.4" ]; then
                export LUA_PATH="$sharedir/lua/5.4/?.lua;$sharedir/lua/5.4/?/init.lua;;"
                break
            fi
        done
    fi
    [ -z "$LUA_CPATH" ] && export LUA_CPATH="/usr/lib/lua/5.4/?.so;/usr/local/lib/lua/5.4/?.so;;"
    [ -z "$LUA_PATH" ] && export LUA_PATH="/usr/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?.lua;;"
}
detect_lua_paths

# ===== 启动 =====
JAVA_BIN=$(./select_jdk.sh)/bin/java
LD_PRELOAD="$SO_PATH:$LD_PRELOAD" "$JAVA_BIN" $JAVA_OPTS -cp "$JAR_PATH:$JLINE_PATH" com.luajava.LuaJMain "$@"
