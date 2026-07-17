cmake_minimum_required(VERSION 3.14)

# ========== 从 version.properties 读取版本号 ==========
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.properties" VERSION_LINES)
foreach(line ${VERSION_LINES})
    if(line MATCHES "^PROJECT_VERSION=(.*)$")
        set(PROJECT_VERSION "${CMAKE_MATCH_1}")
    endif()
endforeach()

if(NOT PROJECT_VERSION)
    set(PROJECT_VERSION "2.0.0")
    message(WARNING "PROJECT_VERSION not found in version.properties, using default: ${PROJECT_VERSION}")
endif()

project(LuaJavaNE VERSION ${PROJECT_VERSION} LANGUAGES C)

# ========== 1. 自动检测 Lua 源码 ==========
# 查找 lua/lua-* 目录
file(GLOB LUA_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/lua/lua-*")
if(NOT LUA_DIRS)
    message(FATAL_ERROR "No Lua source directory found in lua/")
endif()

# 使用第一个匹配的目录
list(GET LUA_DIRS 0 LUA_BASE_DIR)
set(LUA_DIR "${LUA_BASE_DIR}/src")

if(NOT EXISTS "${LUA_DIR}")
    message(FATAL_ERROR "Lua source directory not found: ${LUA_DIR}")
endif()

message(STATUS "Using Lua source: ${LUA_DIR}")

# 收集所有 .c 文件
file(GLOB LUA_CORE_SRCS "${LUA_DIR}/*.c")
if(NOT LUA_CORE_SRCS)
    message(FATAL_ERROR "No .c files found in ${LUA_DIR}")
endif()

# 排除 luac.c（如果存在）
list(REMOVE_ITEM LUA_CORE_SRCS "${LUA_DIR}/luac.c")

add_library(lua STATIC ${LUA_CORE_SRCS})
target_include_directories(lua PUBLIC "${LUA_DIR}")
set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(UNIX)
    target_link_libraries(lua PUBLIC m)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
endif()

# ========== 2. 查找 JNI ==========
find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# ========== 3. Sleef SIMD 库（头文件只有，不需要链接） ==========
set(NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native")
set(SLEEF_DIR "${NATIVE_DIR}/libs/sleef")

if(EXISTS "${SLEEF_DIR}/sleef.h")
    set(USE_SLEEF TRUE)
    message(STATUS "Sleef found, enabling SIMD acceleration in clac")
else()
    set(USE_SLEEF FALSE)
    message(STATUS "Sleef not found, clac will use fallback scalar loops")
endif()

# ========== 4. 查找 OpenMP（备选加速方案） ==========
find_package(OpenMP)
if(OpenMP_C_FOUND AND NOT USE_SLEEF)
    set(USE_OPENMP TRUE)
    message(STATUS "OpenMP found, using as fallback SIMD")
else()
    set(USE_OPENMP FALSE)
endif()

# ========== 5. 定义版本宏 ==========
add_definitions(-DPROJECT_VERSION="${PROJECT_VERSION}")

# ========== 6. JNI 桥接库 ==========
add_library(luajava SHARED
    "${NATIVE_DIR}/jni/luajava.c"
    "${NATIVE_DIR}/lualib/lualibjava.c"
    "${NATIVE_DIR}/lualib/lualib_async.c"
    "${NATIVE_DIR}/lualib/lualib_clac.c"
    "${NATIVE_DIR}/lualib/lualib_gc.c"
    "${NATIVE_DIR}/lualib/lua_custom_init.c"
)

target_include_directories(luajava PRIVATE
    "${NATIVE_DIR}/jni"
    "${NATIVE_DIR}/lualib"
    "${LUA_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/jni"
    ${JNI_INCLUDE_DIRS}
)

# 添加 Sleef 头文件路径
if(USE_SLEEF)
    target_include_directories(luajava PRIVATE "${SLEEF_DIR}")
    target_compile_definitions(luajava PRIVATE CLAC_USE_SLEEF)
endif()

target_link_libraries(luajava PRIVATE lua ${JNI_LIBRARIES})

if(USE_OPENMP)
    target_compile_definitions(luajava PRIVATE CLAC_USE_OPENMP)
    target_link_libraries(luajava PRIVATE OpenMP::OpenMP_C)
endif()

# ========== 7. 平台特定的动态库后缀 ==========
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LIB_SUFFIX ".dylib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LIB_SUFFIX ".dll")
else()
    set(LIB_SUFFIX ".so")
endif()

set_target_properties(luajava PROPERTIES
    PREFIX ""
    OUTPUT_NAME "luajava"
    SUFFIX "${LIB_SUFFIX}"
)

set_target_properties(luajava PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ========== 8. 编译选项 ==========
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(luajava PRIVATE -O3)
    # 如果使用 Sleef，启用对应的 SIMD 指令集
    if(USE_SLEEF)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            target_compile_options(luajava PRIVATE -march=armv8-a+fp+simd)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
            target_compile_options(luajava PRIVATE -mavx2 -mfma -mf16c)
        endif()
    endif()
endif()

# ========== 9. 打印构建信息 ==========
message(STATUS "LuaJavaNE build configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Lua source dir: ${LUA_DIR}")
message(STATUS "  Lua source files: ${LUA_CORE_SRCS}")
message(STATUS "  JNI include: ${JNI_INCLUDE_DIRS}")
message(STATUS "  JNI libraries: ${JNI_LIBRARIES}")
message(STATUS "  Dynamic library suffix: ${LIB_SUFFIX}")
message(STATUS "  Sleef SIMD support: ${USE_SLEEF}")
message(STATUS "  OpenMP support: ${USE_OPENMP}")