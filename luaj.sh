#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SO_PATH="$SCRIPT_DIR/build/luajava.so"
JAR_PATH="$SCRIPT_DIR/luajava.jar"
JLINE_PATH="$SCRIPT_DIR/lib/jline.jar"

if [ -f "$SO_PATH" ]; then
    JAVA_OPTS="-Dluajava.library.path=$SO_PATH"
fi

JAVA_OPTS="$JAVA_OPTS -Dorg.jline.terminal.jna=false -Dorg.jline.terminal.jansi=false -Dorg.jline.terminal.ffm=false -Djline.native=false"
JAVA_OPTS="$JAVA_OPTS -Dluajava.cc=$(cc --version 2>/dev/null | head -1 | tr ' ' '_' || echo Unknown)"

java $JAVA_OPTS -cp "$JAR_PATH:$JLINE_PATH" com.luajava.LuaJMain "$@"
