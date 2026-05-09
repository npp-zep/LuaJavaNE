cmake_minimum_required(VERSION 3.14)
project(LuaJavaNE VERSION 1.0 LANGUAGES C)

# ========== 1. Lua 5.4.8 源码 ==========
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Lua5.4.8")

file(GLOB LUA_CORE_SRCS
    "${LUA_DIR}/lapi.c"
    "${LUA_DIR}/lauxlib.c"
    "${LUA_DIR}/lbaselib.c"
    "${LUA_DIR}/lcode.c"
    "${LUA_DIR}/lcorolib.c"
    "${LUA_DIR}/lctype.c"
    "${LUA_DIR}/ldblib.c"
    "${LUA_DIR}/ldebug.c"
    "${LUA_DIR}/ldo.c"
    "${LUA_DIR}/ldump.c"
    "${LUA_DIR}/lfunc.c"
    "${LUA_DIR}/lgc.c"
    "${LUA_DIR}/linit.c"
    "${LUA_DIR}/liolib.c"
    "${LUA_DIR}/llex.c"
    "${LUA_DIR}/lmathlib.c"
    "${LUA_DIR}/lmem.c"
    "${LUA_DIR}/loadlib.c"
    "${LUA_DIR}/lobject.c"
    "${LUA_DIR}/lopcodes.c"
    "${LUA_DIR}/loslib.c"
    "${LUA_DIR}/lparser.c"
    "${LUA_DIR}/lstate.c"
    "${LUA_DIR}/lstring.c"
    "${LUA_DIR}/lstrlib.c"
    "${LUA_DIR}/ltable.c"
    "${LUA_DIR}/ltablib.c"
    "${LUA_DIR}/ltm.c"
    "${LUA_DIR}/lundump.c"
    "${LUA_DIR}/lutf8lib.c"
    "${LUA_DIR}/lvm.c"
    "${LUA_DIR}/lzio.c"
    "${LUA_DIR}/lualibjava.c"  # 新增的 java 库
    "${LUA_DIR}/lualib_async.c"  # runAsync
    "${LUA_DIR}/lualib_clac.c"
)

add_library(lua STATIC ${LUA_CORE_SRCS})
target_include_directories(lua PUBLIC "${LUA_DIR}")

# 强制生成位置无关代码（ARM64 必须）
set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(UNIX AND NOT APPLE)
    target_link_libraries(lua PUBLIC m)
endif()

target_compile_definitions(lua PRIVATE
    LUA_USE_LINUX    # Termux 是 Linux；macOS 换成 LUA_USE_MACOSX
)

# ========== 2. 查找 JNI ==========
find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# ========== 3. JNI 桥接库 ==========
set(JNI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/jni")

add_library(luajava SHARED
    "${JNI_DIR}/luajava.c"
)

target_include_directories(luajava PRIVATE
    "${JNI_DIR}"
    "${LUA_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src_jni"
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(luajava PRIVATE lua ${JNI_LIBRARIES})

set_target_properties(luajava PROPERTIES
    PREFIX ""
    OUTPUT_NAME "luajava"
)

if(APPLE)
    set_target_properties(luajava PROPERTIES SUFFIX ".dylib")
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
elseif(WIN32)
    set_target_properties(luajava PROPERTIES SUFFIX ".dll")
    target_compile_definitions(lua PRIVATE LUA_USE_WINDOWS)
else()
    set_target_properties(luajava PROPERTIES SUFFIX ".so")
endif()