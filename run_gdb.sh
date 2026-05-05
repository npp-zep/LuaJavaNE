#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SO_PATH="$SCRIPT_DIR/build/luajava.so"
JAR_PATH="$SCRIPT_DIR/luajava.jar"
JLINE_PATH="$SCRIPT_DIR/lib/jline.jar"

gdb -ex run -ex bt -ex quit --args \
  java -Dluajava.library.path=$SO_PATH -cp "$JAR_PATH:$JLINE_PATH" com.luajava.LuaJMain test_runasync.lua
