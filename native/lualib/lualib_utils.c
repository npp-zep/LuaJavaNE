// native/lualib/lualib_utils.c
// 高精度时间和休眠工具库

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifdef _WIN32
    #include <windows.h>
    #include <sys/timeb.h>
#else
    #include <time.h>
    #include <sys/time.h>
    #include <unistd.h>
    #include <errno.h>
#endif

#include <string.h>
#include <stdio.h>

// ========== 辅助函数：获取当前高精度时间 ==========

static double get_precise_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    if (QueryPerformanceFrequency(&freq)) {
        QueryPerformanceCounter(&counter);
        return (double)counter.QuadPart / (double)freq.QuadPart;
    } else {
        FILETIME ft;
        ULARGE_INTEGER ui;
        GetSystemTimeAsFileTime(&ft);
        ui.LowPart = ft.dwLowDateTime;
        ui.HighPart = ft.dwHighDateTime;
        const unsigned long long EPOCH_DIFF = 116444736000000000ULL;
        return (double)(ui.QuadPart - EPOCH_DIFF) / 10000000.0;
    }
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
    }
#endif
}

// ========== 计时器方法 ==========

// timer:elapsed() 的实现
static int utils_timer_elapsed(lua_State *L) {
    // 栈: [self]
    lua_getfield(L, 1, "start_time");  // 栈: [self, start_time]
    double start = lua_tonumber(L, -1);
    lua_pop(L, 1);  // 栈: [self]
    
    double now = get_precise_time();
    lua_pushnumber(L, now - start);  // 栈: [self, result]
    return 1;  // 返回 result
}

// timer:lap() 的实现 - 修复版
static int utils_timer_lap(lua_State *L) {
    // 栈: [self]
    lua_getfield(L, 1, "last_time");  // 栈: [self, last_time]
    double last = lua_tonumber(L, -1);
    lua_pop(L, 1);  // 栈: [self]
    
    double now = get_precise_time();
    double lap = now - last;
    
    // 更新 last_time
    // 栈: [self]
    lua_pushstring(L, "last_time");  // 栈: [self, "last_time"]
    lua_pushnumber(L, now);          // 栈: [self, "last_time", now]
    lua_settable(L, -3);             // 栈: [self] (消耗了 "last_time" 和 now)
    
    // 返回 lap
    lua_pushnumber(L, lap);          // 栈: [self, lap]
    return 1;  // 返回 lap
}

// timer:reset() 的实现 - 修复版
static int utils_timer_reset(lua_State *L) {
    // 栈: [self]
    double now = get_precise_time();
    
    // 更新 start_time
    // 栈: [self]
    lua_pushstring(L, "start_time");  // 栈: [self, "start_time"]
    lua_pushnumber(L, now);           // 栈: [self, "start_time", now]
    lua_settable(L, -3);              // 栈: [self]
    
    // 更新 last_time
    lua_pushstring(L, "last_time");   // 栈: [self, "last_time"]
    lua_pushnumber(L, now);           // 栈: [self, "last_time", now]
    lua_settable(L, -3);              // 栈: [self]
    
    return 0;  // 无返回值
}

// ========== 高精度时间函数 ==========

/**
 * 获取高精度时间（纳秒级精度）
 * 返回: 秒 (double)，包含小数部分
 * 使用 CLOCK_REALTIME (POSIX) 或 QPC (Windows)
 */
static int utils_ns_time(lua_State *L) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq)) {
        // 回退到 GetSystemTimeAsFileTime
        FILETIME ft;
        ULARGE_INTEGER ui;
        GetSystemTimeAsFileTime(&ft);
        ui.LowPart = ft.dwLowDateTime;
        ui.HighPart = ft.dwHighDateTime;
        const unsigned long long EPOCH_DIFF = 116444736000000000ULL;
        double seconds = (double)(ui.QuadPart - EPOCH_DIFF) / 10000000.0;
        lua_pushnumber(L, seconds);
        return 1;
    }
    QueryPerformanceCounter(&counter);
    double seconds = (double)counter.QuadPart / (double)freq.QuadPart;
    lua_pushnumber(L, seconds);
    return 1;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        // 回退到 gettimeofday
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double seconds = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
        lua_pushnumber(L, seconds);
        return 1;
    }
    double seconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    lua_pushnumber(L, seconds);
    return 1;
#endif
}

/**
 * 获取单调时间（不受系统时间调整影响）
 * 返回: 秒 (double)
 * 使用 CLOCK_MONOTONIC (POSIX) 或 QPC (Windows)
 */
static int utils_monotonic_time(lua_State *L) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq)) {
        // 回退到 GetTickCount64
        double seconds = (double)GetTickCount64() / 1000.0;
        lua_pushnumber(L, seconds);
        return 1;
    }
    QueryPerformanceCounter(&counter);
    double seconds = (double)counter.QuadPart / (double)freq.QuadPart;
    lua_pushnumber(L, seconds);
    return 1;
#elif defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        // 回退到 gettimeofday
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double seconds = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
        lua_pushnumber(L, seconds);
        return 1;
    }
    double seconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    lua_pushnumber(L, seconds);
    return 1;
#else
    // 回退到 gettimeofday
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double seconds = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
    lua_pushnumber(L, seconds);
    return 1;
#endif
}

/**
 * 获取当前时间戳（秒，整数）
 */
static int utils_timestamp(lua_State *L) {
#ifdef _WIN32
    time_t t = time(NULL);
    lua_pushinteger(L, (lua_Integer)t);
    return 1;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        lua_pushinteger(L, (lua_Integer)ts.tv_sec);
        return 1;
    }
    time_t t = time(NULL);
    lua_pushinteger(L, (lua_Integer)t);
    return 1;
#endif
}

/**
 * 获取当前时间戳（毫秒）
 */
static int utils_timestamp_ms(lua_State *L) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ui;
    GetSystemTimeAsFileTime(&ft);
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    const unsigned long long EPOCH_DIFF = 116444736000000000ULL;
    unsigned long long ms = (ui.QuadPart - EPOCH_DIFF) / 10000;  // 100纳秒 -> 毫秒
    lua_pushinteger(L, (lua_Integer)ms);
    return 1;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        long long ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        lua_pushinteger(L, (lua_Integer)ms);
        return 1;
    }
    // 回退到 gettimeofday
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long ms = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    lua_pushinteger(L, (lua_Integer)ms);
    return 1;
#endif
}

// ========== 高精度休眠函数 ==========

/**
 * 休眠指定的纳秒数
 * @param nsec 纳秒数（以秒为单位传入，内部转换）
 * @return true 表示成功，false 表示被中断或错误
 */
static int utils_sleep_ns(lua_State *L) {
    long long nsec = (long long)(luaL_checknumber(L, 1) * 1e9);
    if (nsec < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

#ifdef _WIN32
    // Windows: 对于非常短的睡眠，使用忙等待
    if (nsec < 100000) {  // 小于 100 微秒
        LARGE_INTEGER freq, start, end;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        long long target = nsec / 100;  // 100纳秒单位
        while (1) {
            QueryPerformanceCounter(&end);
            long long elapsed = (end.QuadPart - start.QuadPart) * 10000000LL / freq.QuadPart;
            if (elapsed >= target) break;
            if (elapsed + 1000 < target) {  // 如果还有超过10微秒，让出CPU
                Sleep(0);
            }
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    
    // 使用 WaitableTimer 实现高精度
    HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (!timer) {
        // 回退到 Sleep
        Sleep((DWORD)(nsec / 1000000));
        lua_pushboolean(L, 1);
        return 1;
    }
    
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -(nsec / 100);  // 100纳秒单位，负值表示相对时间
    if (!SetWaitableTimer(timer, &dueTime, 0, NULL, NULL, FALSE)) {
        CloseHandle(timer);
        Sleep((DWORD)(nsec / 1000000));
        lua_pushboolean(L, 1);
        return 1;
    }
    
    DWORD result = WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
    lua_pushboolean(L, result == WAIT_OBJECT_0);
    return 1;
#else
    // POSIX: 使用 nanosleep
    struct timespec req, rem;
    req.tv_sec = nsec / 1000000000LL;
    req.tv_nsec = nsec % 1000000000LL;
    
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            // 被信号中断，继续休眠
            req = rem;
            continue;
        }
        // 其他错误
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
#endif
}

/**
 * 休眠指定的微秒数
 * @param usec 微秒数（以秒为单位传入，内部转换）
 * @return true 表示成功
 */
static int utils_sleep_us(lua_State *L) {
    long long usec = (long long)(luaL_checknumber(L, 1) * 1e6);
    if (usec < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // 转换为纳秒并调用 sleep_ns
    lua_pushnumber(L, (lua_Number)usec / 1e9);  // 转为秒
    lua_replace(L, 1);
    return utils_sleep_ns(L);
}

/**
 * 休眠指定的毫秒数
 * @param ms 毫秒数
 * @return true 表示成功
 */
static int utils_sleep_ms(lua_State *L) {
    long long ms = (long long)luaL_checknumber(L, 1);
    if (ms < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
#ifdef _WIN32
    Sleep((DWORD)ms);
    lua_pushboolean(L, 1);
    return 1;
#else
    // 使用 nanosleep
    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
            continue;
        }
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
#endif
}

/**
 * 通用休眠函数（支持小数秒）
 * @param seconds 秒数（浮点数）
 * @return true 表示成功
 */
static int utils_sleep(lua_State *L) {
    double seconds = luaL_checknumber(L, 1);
    if (seconds < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    long long nsec = (long long)(seconds * 1e9);
    lua_pushnumber(L, (lua_Number)nsec / 1e9);
    lua_replace(L, 1);
    return utils_sleep_ns(L);
}

// ========== 高精度计时器 ==========

/**
 * 创建计时器并启动
 * 返回: 计时器表 { 
 *   start_time = number, 
 *   last_time = number,
 *   elapsed = function, 
 *   lap = function, 
 *   reset = function 
 * }
 */
static int utils_timer(lua_State *L) {
    lua_newtable(L);  // 创建 timer 表
    
    // 获取当前时间
    double now = get_precise_time();
    
    // 设置 start_time
    lua_pushstring(L, "start_time");
    lua_pushnumber(L, now);
    lua_settable(L, -3);
    
    // 设置 last_time
    lua_pushstring(L, "last_time");
    lua_pushnumber(L, now);
    lua_settable(L, -3);
    
    // 设置 elapsed 方法
    lua_pushstring(L, "elapsed");
    lua_pushcfunction(L, utils_timer_elapsed);
    lua_settable(L, -3);
    
    // 设置 lap 方法
    lua_pushstring(L, "lap");
    lua_pushcfunction(L, utils_timer_lap);
    lua_settable(L, -3);
    
    // 设置 reset 方法
    lua_pushstring(L, "reset");
    lua_pushcfunction(L, utils_timer_reset);
    lua_settable(L, -3);
    
    return 1;  // 返回 timer 表
}

// ========== 库注册 ==========

static const luaL_Reg utilslib[] = {
    // 时间函数
    {"ns_time",        utils_ns_time},
    {"monotonic_time", utils_monotonic_time},
    {"timestamp",      utils_timestamp},
    {"timestamp_ms",   utils_timestamp_ms},
    
    // 休眠函数
    {"sleep",          utils_sleep},
    {"sleep_ms",       utils_sleep_ms},
    {"sleep_us",       utils_sleep_us},
    {"sleep_ns",       utils_sleep_ns},
    
    // 计时器
    {"timer",          utils_timer},
    
    {NULL, NULL}
};

int luaopen_utils(lua_State *L) {
    luaL_newlib(L, utilslib);
    return 1;
}