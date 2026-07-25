#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ===== 读取版本配置 =====
if [ -f "$SCRIPT_DIR/version.properties" ]; then
    . "$SCRIPT_DIR/version.properties"
fi
MIN_JDK="${MIN_JDK_VERSION:-17}"
REC_JDK="${RECOMMENDED_JDK_VERSION:-21}"

# ===== 平台检测 =====
UNAME_S=$(uname -s)
if [ "$UNAME_S" = "Darwin" ]; then
    LIB_EXT="dylib"
else
    LIB_EXT="so"
fi

# ===== 优先使用当前目录的动态库 =====
SO_PATH=""
if [ -f "$SCRIPT_DIR/luajava.$LIB_EXT" ]; then
    SO_PATH="$SCRIPT_DIR/luajava.$LIB_EXT"
elif [ -f "$SCRIPT_DIR/build/luajava.$LIB_EXT" ]; then
    SO_PATH="$SCRIPT_DIR/build/luajava.$LIB_EXT"
elif [ -f "$SCRIPT_DIR/luajava.so" ]; then
    SO_PATH="$SCRIPT_DIR/luajava.so"
elif [ -f "$SCRIPT_DIR/build/luajava.so" ]; then
    SO_PATH="$SCRIPT_DIR/build/luajava.so"
elif [ -f "$SCRIPT_DIR/luajava.dylib" ]; then
    SO_PATH="$SCRIPT_DIR/luajava.dylib"
elif [ -f "$SCRIPT_DIR/build/luajava.dylib" ]; then
    SO_PATH="$SCRIPT_DIR/build/luajava.dylib"
else
    SO_PATH=""
fi

JAR_PATH="$SCRIPT_DIR/luajava.jar"
JLINE_PATH="$SCRIPT_DIR/lib/jline.jar"

# ============================================================
# 构建 classpath（包含当前目录，让用户编译的 .class 文件能被找到）
# ============================================================
CLASS_PATH=".:$JAR_PATH:$JLINE_PATH"

# ============================================================
# 检测 LuaRocks 和 Lua 路径
# ============================================================

# 1. 检测 LuaRocks 安装
detect_luarocks() {
    local rocks_path=""
    
    if command -v luarocks &>/dev/null; then
        local rocks_tree=$(luarocks path --lr-path 2>/dev/null | grep -oE '^[^;]+' | head -1)
        if [ -n "$rocks_tree" ]; then
            rocks_path=$(echo "$rocks_tree" | sed 's|/lua/5.4/?[^/]*$||' | sed 's|/[^/]*$||')
        fi
    fi
    
    if [ -z "$rocks_path" ]; then
        for rocks in "$HOME/.luarocks" "/usr/local" "/usr" "/opt/homebrew" "/data/data/com.termux/files/usr"; do
            if [ -d "$rocks/lib/lua/5.4" ] || [ -d "$rocks/share/lua/5.4" ]; then
                rocks_path="$rocks"
                break
            fi
        done
    fi
    
    echo "$rocks_path"
}

# 2. 检测 Lua 系统路径
detect_lua_system_paths() {
    local libdir=""
    local sharedir=""
    
    if command -v pkg-config &>/dev/null; then
        libdir=$(pkg-config --variable=libdir lua5.4 2>/dev/null || pkg-config --variable=libdir lua54 2>/dev/null)
        sharedir=$(pkg-config --variable=INSTALL_LMOD lua5.4 2>/dev/null || pkg-config --variable=INSTALL_LMOD lua54 2>/dev/null)
    fi
    
    if [ -z "$libdir" ]; then
        for lib in /data/data/com.termux/files/usr/lib /usr/lib /usr/lib64 /usr/local/lib /opt/homebrew/lib /opt/local/lib; do
            if [ -d "$lib/lua/5.4" ]; then
                libdir="$lib/lua/5.4"
                break
            fi
        done
    fi
    
    if [ -z "$sharedir" ]; then
        for share in /data/data/com.termux/files/usr/share /usr/share /usr/local/share /opt/homebrew/share /opt/local/share; do
            if [ -d "$share/lua/5.4" ]; then
                sharedir="$share/lua/5.4"
                break
            fi
        done
    fi
    
    echo "$libdir:$sharedir"
}

# 3. 构建 LUA_PATH 和 LUA_CPATH
build_lua_paths() {
    local lua_path=""
    local lua_cpath=""
    
    local rocks_path=$(detect_luarocks)
    local sys_paths=$(detect_lua_system_paths)
    local sys_lib=$(echo "$sys_paths" | cut -d: -f1)
    local sys_share=$(echo "$sys_paths" | cut -d: -f2)
    
    # ---- 构建 LUA_PATH ----
    local path_entries=()
    
    # 当前目录和脚本目录（优先）
    path_entries+=("$SCRIPT_DIR/?.lua")
    path_entries+=("$SCRIPT_DIR/?/init.lua")
    path_entries+=("./?.lua")
    path_entries+=("./?/init.lua")
    
    # LuaRocks 路径
    if [ -n "$rocks_path" ]; then
        path_entries+=("$rocks_path/share/lua/5.4/?.lua")
        path_entries+=("$rocks_path/share/lua/5.4/?/init.lua")
        path_entries+=("$rocks_path/lib/lua/5.4/?.lua")
        path_entries+=("$rocks_path/lib/lua/5.4/?/init.lua")
    fi
    
    # 系统路径
    if [ -n "$sys_share" ] && [ "$sys_share" != "$rocks_path/share/lua/5.4" ]; then
        path_entries+=("$sys_share/?.lua")
        path_entries+=("$sys_share/?/init.lua")
    fi
    
    # 额外路径
    path_entries+=("/data/data/com.termux/files/usr/share/lua/5.4/?.lua")
    path_entries+=("/data/data/com.termux/files/usr/share/lua/5.4/?/init.lua")
    path_entries+=("/usr/share/lua/5.4/?.lua")
    path_entries+=("/usr/local/share/lua/5.4/?.lua")
    
    # 合并
    lua_path=""
    for entry in "${path_entries[@]}"; do
        if [ -n "$lua_path" ]; then
            lua_path="$lua_path;$entry"
        else
            lua_path="$entry"
        fi
    done
    
    # ---- 构建 LUA_CPATH（包含当前目录，方便加载 C 扩展） ----
    local cpath_entries=()
    
    # 当前目录（优先）
    cpath_entries+=("./?.so")
    cpath_entries+=("./?.$LIB_EXT")
    cpath_entries+=("$SCRIPT_DIR/?.so")
    cpath_entries+=("$SCRIPT_DIR/?.$LIB_EXT")
    
    # LuaRocks 路径
    if [ -n "$rocks_path" ]; then
        cpath_entries+=("$rocks_path/lib/lua/5.4/?.so")
        cpath_entries+=("$rocks_path/lib/lua/5.4/?.$LIB_EXT")
        cpath_entries+=("$rocks_path/lib/lua/5.4/loadall.so")
    fi
    
    # 系统路径
    if [ -n "$sys_lib" ] && [ "$sys_lib" != "$rocks_path/lib/lua/5.4" ]; then
        cpath_entries+=("$sys_lib/?.so")
        cpath_entries+=("$sys_lib/?.$LIB_EXT")
    fi
    
    # 额外路径
    cpath_entries+=("/data/data/com.termux/files/usr/lib/lua/5.4/?.so")
    cpath_entries+=("/usr/lib/lua/5.4/?.so")
    cpath_entries+=("/usr/local/lib/lua/5.4/?.so")
    cpath_entries+=("/usr/lib/lua/5.4/loadall.so")
    
    # 合并
    lua_cpath=""
    for entry in "${cpath_entries[@]}"; do
        if [ -n "$lua_cpath" ]; then
            lua_cpath="$lua_cpath;$entry"
        else
            lua_cpath="$entry"
        fi
    done
    
    # 直接导出到环境
    export LUA_PATH="$lua_path"
    export LUA_CPATH="$lua_cpath"
}

# 执行构建
build_lua_paths

# ============================================================
# 构建 Java 启动参数
# ============================================================

JAVA_OPTS=""
if [ -n "$SO_PATH" ]; then
    JAVA_OPTS="-Dluajava.library.path=$SO_PATH"
fi
JAVA_OPTS="$JAVA_OPTS -Dorg.jline.terminal.jna=false -Dorg.jline.terminal.jansi=false -Dorg.jline.terminal.ffm=false -Djline.native=false"

# 传递 Lua 路径到 Java
if [ -n "$LUA_PATH" ]; then
    JAVA_OPTS="$JAVA_OPTS -Dluajava.lua.path=\"$LUA_PATH\""
fi
if [ -n "$LUA_CPATH" ]; then
    JAVA_OPTS="$JAVA_OPTS -Dluajava.lua.cpath=\"$LUA_CPATH\""
fi

# 传递版本信息
if [ -f "$SCRIPT_DIR/version.properties" ]; then
    JAVA_OPTS="$JAVA_OPTS -Dluajava.version=$PROJECT_VERSION"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.name=$PROJECT_NAME"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.copyright=$PROJECT_COPYRIGHT"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.license=$PROJECT_LICENSE"
    JAVA_OPTS="$JAVA_OPTS -Dluajava.url=$PROJECT_URL"
fi

# ============================================================
# 选择 JDK 并启动
# ============================================================

if [ -f "$SCRIPT_DIR/select_jdk.sh" ]; then
    chmod +x "$SCRIPT_DIR/select_jdk.sh" 2>/dev/null
    JAVA_HOME_DIR=$("$SCRIPT_DIR/select_jdk.sh" 2>/dev/null)
    if [ -n "$JAVA_HOME_DIR" ] && [ -f "$JAVA_HOME_DIR/bin/java" ]; then
        JAVA_BIN="$JAVA_HOME_DIR/bin/java"
    else
        JAVA_BIN="java"
    fi
else
    JAVA_BIN="java"
fi

# 启动（使用包含当前目录的 CLASS_PATH）
if [ -n "$SO_PATH" ]; then
    LD_PRELOAD="$SO_PATH:$LD_PRELOAD" "$JAVA_BIN" $JAVA_OPTS -cp "$CLASS_PATH" com.luajava.LuaJMain "$@"
else
    "$JAVA_BIN" $JAVA_OPTS -cp "$CLASS_PATH" com.luajava.LuaJMain "$@"
fi