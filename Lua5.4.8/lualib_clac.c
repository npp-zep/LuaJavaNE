// Lua5.4.8/lualib_clac.c — C 端高性能数学运算（完整版）
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

// ========== 基本运算 ==========
#define CLAC_OP(name, op) \
static int clac_##name(lua_State* L) { \
    lua_Number a = luaL_checknumber(L, 1); \
    lua_Number b = luaL_checknumber(L, 2); \
    lua_pushnumber(L, a op b); \
    return 1; \
}

CLAC_OP(add, +)
CLAC_OP(sub, -)
CLAC_OP(mul, *)
CLAC_OP(div, /)

// ========== 取整 ==========
static int clac_abs(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, fabs(x));
    return 1;
}

static int clac_floor(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, floor(x));
    return 1;
}

static int clac_ceil(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, ceil(x));
    return 1;
}

static int clac_round(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, round(x));
    return 1;
}

// ========== 极值 ==========
static int clac_min(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 2) { lua_pushnil(L); return 1; }
    lua_Number min = luaL_checknumber(L, 1);
    for (int i = 2; i <= n; i++) {
        lua_Number v = luaL_checknumber(L, i);
        if (v < min) min = v;
    }
    lua_pushnumber(L, min);
    return 1;
}

static int clac_max(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 2) { lua_pushnil(L); return 1; }
    lua_Number max = luaL_checknumber(L, 1);
    for (int i = 2; i <= n; i++) {
        lua_Number v = luaL_checknumber(L, i);
        if (v > max) max = v;
    }
    lua_pushnumber(L, max);
    return 1;
}

// ========== 幂与对数 ==========
static int clac_pow(lua_State* L) {
    lua_Number a = luaL_checknumber(L, 1);
    lua_Number b = luaL_checknumber(L, 2);
    lua_pushnumber(L, pow(a, b));
    return 1;
}

static int clac_sqrt(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, sqrt(x));
    return 1;
}

static int clac_cbrt(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, cbrt(x));
    return 1;
}

static int clac_log(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    if (lua_gettop(L) >= 2) {
        lua_Number base = luaL_checknumber(L, 2);
        lua_pushnumber(L, log(x) / log(base));
    } else {
        lua_pushnumber(L, log(x));
    }
    return 1;
}

static int clac_log10(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_pushnumber(L, log10(x));
    return 1;
}

// ========== 三角函数 ==========
static int clac_sin(lua_State* L)  { lua_pushnumber(L, sin(luaL_checknumber(L, 1))); return 1; }
static int clac_cos(lua_State* L)  { lua_pushnumber(L, cos(luaL_checknumber(L, 1))); return 1; }
static int clac_tan(lua_State* L)  { lua_pushnumber(L, tan(luaL_checknumber(L, 1))); return 1; }
static int clac_asin(lua_State* L) { lua_pushnumber(L, asin(luaL_checknumber(L, 1))); return 1; }
static int clac_acos(lua_State* L) { lua_pushnumber(L, acos(luaL_checknumber(L, 1))); return 1; }
static int clac_atan(lua_State* L) { lua_pushnumber(L, atan(luaL_checknumber(L, 1))); return 1; }

// ========== 双曲函数 ==========
static int clac_sinh(lua_State* L) { lua_pushnumber(L, sinh(luaL_checknumber(L, 1))); return 1; }
static int clac_cosh(lua_State* L) { lua_pushnumber(L, cosh(luaL_checknumber(L, 1))); return 1; }
static int clac_tanh(lua_State* L) { lua_pushnumber(L, tanh(luaL_checknumber(L, 1))); return 1; }

// ========== 常量 ==========
static int clac_pi(lua_State* L)   { lua_pushnumber(L, M_PI); return 1; }
static int clac_e(lua_State* L)    { lua_pushnumber(L, M_E); return 1; }

// ========== 随机数 ==========
static int clac_random(lua_State* L) {
    int n = lua_gettop(L);
    if (n == 0) {
        lua_pushnumber(L, (lua_Number)rand() / RAND_MAX);
    } else if (n == 1) {
        lua_Integer max = luaL_checkinteger(L, 1);
        lua_pushinteger(L, rand() % max + 1);
    } else {
        lua_Integer min = luaL_checkinteger(L, 1);
        lua_Integer max = luaL_checkinteger(L, 2);
        lua_pushinteger(L, rand() % (max - min + 1) + min);
    }
    return 1;
}

static int clac_seed(lua_State* L) {
    srand((unsigned)time(NULL));
    return 0;
}

// ========== 角度转换 ==========
static int clac_deg(lua_State* L) {
    lua_pushnumber(L, luaL_checknumber(L, 1) * 180.0 / M_PI);
    return 1;
}

static int clac_rad(lua_State* L) {
    lua_pushnumber(L, luaL_checknumber(L, 1) * M_PI / 180.0);
    return 1;
}

// ========== 注册表 ==========
static const luaL_Reg claclib[] = {
    {"add",    clac_add},
    {"sub",    clac_sub},
    {"mul",    clac_mul},
    {"div",    clac_div},
    {"abs",    clac_abs},
    {"floor",  clac_floor},
    {"ceil",   clac_ceil},
    {"round",  clac_round},
    {"min",    clac_min},
    {"max",    clac_max},
    {"pow",    clac_pow},
    {"sqrt",   clac_sqrt},
    {"cbrt",   clac_cbrt},
    {"log",    clac_log},
    {"log10",  clac_log10},
    {"sin",    clac_sin},
    {"cos",    clac_cos},
    {"tan",    clac_tan},
    {"asin",   clac_asin},
    {"acos",   clac_acos},
    {"atan",   clac_atan},
    {"sinh",   clac_sinh},
    {"cosh",   clac_cosh},
    {"tanh",   clac_tanh},
    {"pi",     clac_pi},
    {"e",      clac_e},
    {"random", clac_random},
    {"seed",   clac_seed},
    {"deg",    clac_deg},
    {"rad",    clac_rad},
    {NULL, NULL}
};

int luaopen_clac(lua_State* L) {
    luaL_newlib(L, claclib);
    clac_seed(L);  // 自动初始化随机种子
    return 1;
}
