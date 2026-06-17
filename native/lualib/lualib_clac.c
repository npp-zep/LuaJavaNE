// Lua5.4.8/lualib_clac.c — C 端高性能数学运算（完整版，含 ClacArray 批量处理）
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define CLAC_ARRAY_META "Clac.Array"

typedef struct {
    double* data;
    int     len;
} ClacArray;

// ========== Clac.Array 操作 ==========
static int clac_array(lua_State* L) {
    int n = (int)luaL_checkinteger(L, 1);
    ClacArray* arr = (ClacArray*)lua_newuserdatauv(L, sizeof(ClacArray), 0);
    arr->data = (double*)calloc(n, sizeof(double));
    arr->len  = n;
    luaL_getmetatable(L, CLAC_ARRAY_META);
    lua_setmetatable(L, -2);
    return 1;
}

static int clac_array_index(lua_State* L) {
    ClacArray* arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 1 || idx > arr->len) return luaL_error(L, "index out of range");
    lua_pushnumber(L, arr->data[idx - 1]);
    return 1;
}

static int clac_array_newindex(lua_State* L) {
    ClacArray* arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 1 || idx > arr->len) return luaL_error(L, "index out of range");
    arr->data[idx - 1] = luaL_checknumber(L, 3);
    return 0;
}

static int clac_array_len(lua_State* L) {
    ClacArray* arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    lua_pushinteger(L, arr->len);
    return 1;
}

static int clac_array_gc(lua_State* L) {
    ClacArray* arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    free(arr->data);
    return 0;
}

// 批量操作：直接在 userdata 的数据块上做 SIMD 或 C 循环
#define DEFINE_BATCH_OP(name, op) \
static int clac_batch_##name(lua_State* L) { \
    ClacArray* a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META); \
    ClacArray* b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META); \
    if (a->len != b->len) return luaL_error(L, "size mismatch"); \
    ClacArray* c = (ClacArray*)lua_newuserdatauv(L, sizeof(ClacArray), 0); \
    c->len  = a->len; \
    c->data = (double*)malloc(c->len * sizeof(double)); \
    luaL_getmetatable(L, CLAC_ARRAY_META); \
    lua_setmetatable(L, -2); \
    double* __restrict da = a->data; \
    double* __restrict db = b->data; \
    double* __restrict dc = c->data; \
    int n = a->len; \
    for (int i = 0; i < n; i++) { dc[i] = da[i] op db[i]; } \
    return 1; \
}

DEFINE_BATCH_OP(add, +)
DEFINE_BATCH_OP(sub, -)
DEFINE_BATCH_OP(mul, *)
DEFINE_BATCH_OP(div, /)

static int clac_batch_sin(lua_State* L) {
    ClacArray* a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray* c = (ClacArray*)lua_newuserdatauv(L, sizeof(ClacArray), 0);
    c->len  = a->len;
    c->data = (double*)malloc(c->len * sizeof(double));
    luaL_getmetatable(L, CLAC_ARRAY_META);
    lua_setmetatable(L, -2);
    double* __restrict da = a->data;
    double* __restrict dc = c->data;
    int n = a->len;
    for (int i = 0; i < n; i++) { dc[i] = sin(da[i]); }
    return 1;
}

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
static int clac_abs(lua_State* L) { lua_pushnumber(L, fabs(luaL_checknumber(L, 1))); return 1; }
static int clac_floor(lua_State* L) { lua_pushnumber(L, floor(luaL_checknumber(L, 1))); return 1; }
static int clac_ceil(lua_State* L) { lua_pushnumber(L, ceil(luaL_checknumber(L, 1))); return 1; }
static int clac_round(lua_State* L) { lua_pushnumber(L, round(luaL_checknumber(L, 1))); return 1; }
static int clac_trunc(lua_State* L) { lua_pushnumber(L, trunc(luaL_checknumber(L, 1))); return 1; }

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

// ========== 幂、指数、对数 ==========
static int clac_pow(lua_State* L) { lua_Number a = luaL_checknumber(L, 1); lua_Number b = luaL_checknumber(L, 2); lua_pushnumber(L, pow(a, b)); return 1; }
static int clac_sqrt(lua_State* L) { lua_pushnumber(L, sqrt(luaL_checknumber(L, 1))); return 1; }
static int clac_cbrt(lua_State* L) { lua_pushnumber(L, cbrt(luaL_checknumber(L, 1))); return 1; }
static int clac_hypot(lua_State* L) { lua_Number a = luaL_checknumber(L, 1); lua_Number b = luaL_checknumber(L, 2); lua_pushnumber(L, hypot(a, b)); return 1; }
static int clac_exp(lua_State* L) { lua_pushnumber(L, exp(luaL_checknumber(L, 1))); return 1; }
static int clac_exp2(lua_State* L) { lua_pushnumber(L, exp2(luaL_checknumber(L, 1))); return 1; }
static int clac_expm1(lua_State* L) { lua_pushnumber(L, expm1(luaL_checknumber(L, 1))); return 1; }
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
static int clac_log10(lua_State* L) { lua_pushnumber(L, log10(luaL_checknumber(L, 1))); return 1; }
static int clac_log2(lua_State* L) { lua_pushnumber(L, log2(luaL_checknumber(L, 1))); return 1; }
static int clac_log1p(lua_State* L) { lua_pushnumber(L, log1p(luaL_checknumber(L, 1))); return 1; }

// ========== 三角函数 ==========
static int clac_sin(lua_State* L) { lua_pushnumber(L, sin(luaL_checknumber(L, 1))); return 1; }
static int clac_cos(lua_State* L) { lua_pushnumber(L, cos(luaL_checknumber(L, 1))); return 1; }
static int clac_tan(lua_State* L) { lua_pushnumber(L, tan(luaL_checknumber(L, 1))); return 1; }
static int clac_asin(lua_State* L) { lua_pushnumber(L, asin(luaL_checknumber(L, 1))); return 1; }
static int clac_acos(lua_State* L) { lua_pushnumber(L, acos(luaL_checknumber(L, 1))); return 1; }
static int clac_atan(lua_State* L) { lua_pushnumber(L, atan(luaL_checknumber(L, 1))); return 1; }
static int clac_atan2(lua_State* L) {
    lua_Number y = luaL_checknumber(L, 1);
    lua_Number x = luaL_checknumber(L, 2);
    lua_pushnumber(L, atan2(y, x));
    return 1;
}

// ========== 双曲函数 ==========
static int clac_sinh(lua_State* L) { lua_pushnumber(L, sinh(luaL_checknumber(L, 1))); return 1; }
static int clac_cosh(lua_State* L) { lua_pushnumber(L, cosh(luaL_checknumber(L, 1))); return 1; }
static int clac_tanh(lua_State* L) { lua_pushnumber(L, tanh(luaL_checknumber(L, 1))); return 1; }
static int clac_asinh(lua_State* L) { lua_pushnumber(L, asinh(luaL_checknumber(L, 1))); return 1; }
static int clac_acosh(lua_State* L) { lua_pushnumber(L, acosh(luaL_checknumber(L, 1))); return 1; }
static int clac_atanh(lua_State* L) { lua_pushnumber(L, atanh(luaL_checknumber(L, 1))); return 1; }

// ========== 特殊函数 ==========
static int clac_erf(lua_State* L) { lua_pushnumber(L, erf(luaL_checknumber(L, 1))); return 1; }
static int clac_erfc(lua_State* L) { lua_pushnumber(L, erfc(luaL_checknumber(L, 1))); return 1; }
static int clac_tgamma(lua_State* L) { lua_pushnumber(L, tgamma(luaL_checknumber(L, 1))); return 1; }
static int clac_lgamma(lua_State* L) { lua_pushnumber(L, lgamma(luaL_checknumber(L, 1))); return 1; }

// ========== 浮点操作 ==========
static int clac_copysign(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_Number y = luaL_checknumber(L, 2);
    lua_pushnumber(L, copysign(x, y));
    return 1;
}
static int clac_fmod(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_Number y = luaL_checknumber(L, 2);
    lua_pushnumber(L, fmod(x, y));
    return 1;
}
static int clac_remainder(lua_State* L) {
    lua_Number x = luaL_checknumber(L, 1);
    lua_Number y = luaL_checknumber(L, 2);
    lua_pushnumber(L, remainder(x, y));
    return 1;
}
static int clac_nextafter(lua_State* L) {
    lua_Number from = luaL_checknumber(L, 1);
    lua_Number to   = luaL_checknumber(L, 2);
    lua_pushnumber(L, nextafter(from, to));
    return 1;
}

// ========== 常量 ==========
static int clac_pi(lua_State* L) { lua_pushnumber(L, M_PI); return 1; }
static int clac_e(lua_State* L)  { lua_pushnumber(L, M_E);  return 1; }

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
static int clac_deg(lua_State* L) { lua_pushnumber(L, luaL_checknumber(L, 1) * 180.0 / M_PI); return 1; }
static int clac_rad(lua_State* L) { lua_pushnumber(L, luaL_checknumber(L, 1) * M_PI / 180.0); return 1; }

// ========== 注册表 ==========
static const luaL_Reg claclib[] = {
    {"array",        clac_array},
    {"batch_add",    clac_batch_add},
    {"batch_sub",    clac_batch_sub},
    {"batch_mul",    clac_batch_mul},
    {"batch_div",    clac_batch_div},
    {"batch_sin",    clac_batch_sin},

    {"add",    clac_add},
    {"sub",    clac_sub},
    {"mul",    clac_mul},
    {"div",    clac_div},

    {"abs",    clac_abs},
    {"floor",  clac_floor},
    {"ceil",   clac_ceil},
    {"round",  clac_round},
    {"trunc",  clac_trunc},

    {"min",    clac_min},
    {"max",    clac_max},

    {"pow",    clac_pow},
    {"sqrt",   clac_sqrt},
    {"cbrt",   clac_cbrt},
    {"hypot",  clac_hypot},
    {"exp",    clac_exp},
    {"exp2",   clac_exp2},
    {"expm1",  clac_expm1},
    {"log",    clac_log},
    {"log10",  clac_log10},
    {"log2",   clac_log2},
    {"log1p",  clac_log1p},

    {"sin",    clac_sin},
    {"cos",    clac_cos},
    {"tan",    clac_tan},
    {"asin",   clac_asin},
    {"acos",   clac_acos},
    {"atan",   clac_atan},
    {"atan2",  clac_atan2},

    {"sinh",   clac_sinh},
    {"cosh",   clac_cosh},
    {"tanh",   clac_tanh},
    {"asinh",  clac_asinh},
    {"acosh",  clac_acosh},
    {"atanh",  clac_atanh},

    {"erf",    clac_erf},
    {"erfc",   clac_erfc},
    {"tgamma", clac_tgamma},
    {"lgamma", clac_lgamma},

    {"copysign", clac_copysign},
    {"fmod",   clac_fmod},
    {"remainder", clac_remainder},
    {"nextafter", clac_nextafter},

    {"pi",     clac_pi},
    {"e",      clac_e},

    {"random", clac_random},
    {"seed",   clac_seed},

    {"deg",    clac_deg},
    {"rad",    clac_rad},

    {NULL, NULL}
};

int luaopen_clac(lua_State* L) {
    luaL_newmetatable(L, CLAC_ARRAY_META);
    lua_pushstring(L, "__index");    lua_pushcfunction(L, clac_array_index);    lua_settable(L, -3);
    lua_pushstring(L, "__newindex"); lua_pushcfunction(L, clac_array_newindex); lua_settable(L, -3);
    lua_pushstring(L, "__len");      lua_pushcfunction(L, clac_array_len);      lua_settable(L, -3);
    lua_pushstring(L, "__gc");       lua_pushcfunction(L, clac_array_gc);       lua_settable(L, -3);
    lua_pop(L, 1);

    luaL_newlib(L, claclib);
    clac_seed(L);
    return 1;
}