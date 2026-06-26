#!/bin/bash
# 智能 JDK 选择脚本，被 Makefile 和 luaj.sh 共同调用
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 读取版本配置
if [ -f "$SCRIPT_DIR/version.properties" ]; then
    source "$SCRIPT_DIR/version.properties"
fi
MIN_JDK="${MIN_JDK_VERSION:-17}"
REC_JDK="${RECOMMENDED_JDK_VERSION:-17}"

get_version() {
    local java_bin=$1
    [ ! -x "$java_bin" ] && return 1
    local ver=$("$java_bin" -version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    [ -z "$ver" ] && return 1
    echo "$ver" | cut -d. -f1
}

check_version() {
    local major=$(get_version "$1")
    [ -n "$major" ] && [ "$major" -ge "$MIN_JDK" ] 2>/dev/null
}

find_jdk() {
    local candidates=""

    # 1. JAVA_HOME
    if [ -n "$JAVA_HOME" ] && check_version "$JAVA_HOME/bin/java"; then
        candidates="$candidates $JAVA_HOME/bin/java"
    fi

    # 2. 当前 PATH（默认最高优先级）
    local path_java=$(command -v java 2>/dev/null)
    if [ -n "$path_java" ] && check_version "$path_java"; then
        candidates="$candidates $path_java"
    fi

    # 3. Termux
    for jdk in /data/data/com.termux/files/usr/lib/jvm/openjdk-*; do
        [ -x "$jdk/bin/java" ] && check_version "$jdk/bin/java" && candidates="$candidates $jdk/bin/java"
    done

    # 4. 常见 Linux 路径
    for jdk_base in /usr/lib/jvm /usr/local/lib/jvm /opt; do
        for jdk in "$jdk_base"/java-*-openjdk-* "$jdk_base"/jdk-* "$jdk_base"/openjdk-* "$jdk_base"/temurin-*; do
            [ -x "$jdk/bin/java" ] && check_version "$jdk/bin/java" && candidates="$candidates $jdk/bin/java"
        done
    done

    # 5. macOS Homebrew
    for jdk in /opt/homebrew/opt/openjdk@*/bin/java /usr/local/opt/openjdk@*/bin/java; do
        [ -x "$jdk" ] && check_version "$jdk" && candidates="$candidates $jdk"
    done

    # 6. sdkman / jabba
    for base in "$HOME/.sdkman/candidates/java" "$HOME/.jabba/jdk"; do
        if [ -d "$base" ]; then
            for jdk in "$base"/*; do
                [ -x "$jdk/bin/java" ] && check_version "$jdk/bin/java" && candidates="$candidates $jdk/bin/java"
            done
        fi
    done

    # 选择最佳：优先推荐版本，其次最高版本
    local best=""
    local best_ver=0
    for java_bin in $candidates; do
        local ver=$(get_version "$java_bin")
        [ -z "$ver" ] && continue
        if [ "$ver" = "$REC_JDK" ]; then
            echo "$java_bin"
            return
        fi
        if [ "$ver" -gt "$best_ver" ]; then
            best_ver=$ver
            best=$java_bin
        fi
    done

    [ -n "$best" ] && echo "$best" || echo "${path_java:-java}"
}

# 输出 JAVA_HOME 路径
JAVA_BIN=$(find_jdk)
JAVA_HOME_DIR=$(dirname "$(dirname "$JAVA_BIN")")
echo "$JAVA_HOME_DIR"
