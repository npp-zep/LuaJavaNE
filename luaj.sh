#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SO_PATH="$SCRIPT_DIR/build/luajava.so"
JAR_PATH="$SCRIPT_DIR/luajava.jar"

if [ -f "$SO_PATH" ]; then
    JAVA_OPTS="-Dluajava.library.path=$SO_PATH"
fi

java $JAVA_OPTS -cp "$JAR_PATH" com.luajava.LuaJMain "$@"
