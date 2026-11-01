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

# ===== 关键修改：启用动态库加载 =====
target_compile_definitions(lua PRIVATE LUA_USE_DLOPEN)

if(UNIX)
    target_link_libraries(lua PUBLIC m dl)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
endif()

# ========== 2. 查找 JNI ==========
find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# ========== 3. Sleef SIMD 库（默认启用；x86_64 走 AVX2 4 宽向量 API） ==========
# 说明：clac 批量数学函数使用 Sleef 的 d4 向量 API（Sleef_xxxd4_u10/u35avx2），
# 一次处理 4 个 double；仅在 x86+AVX2 下启用向量路径，其余情况回退标量/libm。
option(LUAJAVA_ENABLE_SLEEF "Enable Sleef AVX2 vector math in clac (falls back to libm)" ON)

set(NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native")
set(SLEEF_DIR "${NATIVE_DIR}/libs/sleef")

set(SLEEF_INCLUDE_DIR "")
set(SLEEF_LIBRARY "")

if(LUAJAVA_ENABLE_SLEEF)
    # 3.1 优先使用仓库内自带的头文件 + 同目录库（版本严格匹配）
    find_library(SLEEF_VENDORED_LIBRARY NAMES sleef libsleef
        PATHS "${SLEEF_DIR}" NO_DEFAULT_PATH)
    if(SLEEF_VENDORED_LIBRARY AND EXISTS "${SLEEF_DIR}/sleef.h")
        set(SLEEF_INCLUDE_DIR "${SLEEF_DIR}")
        set(SLEEF_LIBRARY "${SLEEF_VENDORED_LIBRARY}")
    endif()

    # 3.2 否则使用系统安装的 Sleef（头文件与库成对出现）
    if(NOT SLEEF_LIBRARY)
        find_path(SLEEF_SYSTEM_INCLUDE_DIR NAMES sleef.h)
        find_library(SLEEF_SYSTEM_LIBRARY NAMES sleef libsleef)
        if(SLEEF_SYSTEM_INCLUDE_DIR AND SLEEF_SYSTEM_LIBRARY)
            set(SLEEF_INCLUDE_DIR "${SLEEF_SYSTEM_INCLUDE_DIR}")
            set(SLEEF_LIBRARY "${SLEEF_SYSTEM_LIBRARY}")
        endif()
    endif()
endif()

if(SLEEF_INCLUDE_DIR AND SLEEF_LIBRARY)
    set(USE_SLEEF TRUE)
    message(STATUS "Sleef enabled (${SLEEF_LIBRARY}), batch math uses AVX2 vector API when available")
else()
    set(USE_SLEEF FALSE)
    if(LUAJAVA_ENABLE_SLEEF)
        message(STATUS "Sleef requested but not found (headers and library required), clac will use libm")
    else()
        message(STATUS "Sleef disabled, clac will use libm")
    endif()
endif()

# ========== 4. 查找 OpenMP（批量运算并行加速，可与 Sleef 叠加） ==========
find_package(OpenMP)
if(OpenMP_C_FOUND)
    set(USE_OPENMP TRUE)
    message(STATUS "OpenMP found, batch operations will use parallel loops")
else()
    set(USE_OPENMP FALSE)
    message(STATUS "OpenMP not found, batch operations will run serially")
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
    "${NATIVE_DIR}/lualib/lualib_utils.c"
    "${NATIVE_DIR}/lualib/lua_custom_init.c"
)

target_include_directories(luajava PRIVATE
    "${NATIVE_DIR}/jni"
    "${NATIVE_DIR}/lualib"
    "${LUA_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/jni"
    ${JNI_INCLUDE_DIRS}
)

# 添加 Sleef 头文件路径并链接 Sleef 库
if(USE_SLEEF)
    target_include_directories(luajava PRIVATE "${SLEEF_INCLUDE_DIR}")
    target_compile_definitions(luajava PRIVATE CLAC_USE_SLEEF)
    target_link_libraries(luajava PRIVATE ${SLEEF_LIBRARY})
endif()

target_link_libraries(luajava PRIVATE lua ${JNI_LIBRARIES} dl)

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
    # 手工 SIMD 四则运算路径依赖目标架构指令集，与 Sleef 是否启用无关
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        target_compile_options(luajava PRIVATE -march=armv8-a+fp+simd)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        target_compile_options(luajava PRIVATE -mavx2 -mfma -mf16c)
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