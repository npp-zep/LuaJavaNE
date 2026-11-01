// native/lualib/lualib_clac.c — 完整 C99 <math.h> + Sleef SIMD 加速版本
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

// ========== Sleef SIMD 加速 ==========
// Sleef 只提供部分标量 double 函数（Sleef_xxx_u10/u35/u15）；未提供的函数
// 必须回退到 <math.h>，否则会出现隐式声明并在加载共享库时因符号缺失而失败。
#ifdef CLAC_USE_SLEEF
#include <sleef.h>

#define MATH_FUNC_UNARY(func, x)      CLAC_SLEEF_U_##func(x)
#define MATH_FUNC_BINARY(func, x, y)  CLAC_SLEEF_B_##func(x, y)

// 一元：Sleef 提供标量版本
#define CLAC_SLEEF_U_sqrt(x)       Sleef_sqrt_u35(x)
#define CLAC_SLEEF_U_cbrt(x)       Sleef_cbrt_u10(x)
#define CLAC_SLEEF_U_exp(x)        Sleef_exp_u10(x)
#define CLAC_SLEEF_U_exp2(x)       Sleef_exp2_u10(x)
#define CLAC_SLEEF_U_expm1(x)      Sleef_expm1_u10(x)
#define CLAC_SLEEF_U_log(x)        Sleef_log_u10(x)
#define CLAC_SLEEF_U_log10(x)      Sleef_log10_u10(x)
#define CLAC_SLEEF_U_log2(x)       Sleef_log2_u10(x)
#define CLAC_SLEEF_U_log1p(x)      Sleef_log1p_u10(x)
#define CLAC_SLEEF_U_sin(x)        Sleef_sin_u10(x)
#define CLAC_SLEEF_U_cos(x)        Sleef_cos_u10(x)
#define CLAC_SLEEF_U_tan(x)        Sleef_tan_u10(x)
#define CLAC_SLEEF_U_asin(x)       Sleef_asin_u10(x)
#define CLAC_SLEEF_U_acos(x)       Sleef_acos_u10(x)
#define CLAC_SLEEF_U_atan(x)       Sleef_atan_u10(x)
#define CLAC_SLEEF_U_sinh(x)       Sleef_sinh_u10(x)
#define CLAC_SLEEF_U_cosh(x)       Sleef_cosh_u10(x)
#define CLAC_SLEEF_U_tanh(x)       Sleef_tanh_u10(x)
#define CLAC_SLEEF_U_asinh(x)      Sleef_asinh_u10(x)
#define CLAC_SLEEF_U_acosh(x)      Sleef_acosh_u10(x)
#define CLAC_SLEEF_U_atanh(x)      Sleef_atanh_u10(x)
#define CLAC_SLEEF_U_erf(x)        Sleef_erf_u10(x)
#define CLAC_SLEEF_U_erfc(x)       Sleef_erfc_u15(x)
#define CLAC_SLEEF_U_tgamma(x)     Sleef_tgamma_u10(x)
#define CLAC_SLEEF_U_lgamma(x)     Sleef_lgamma_u10(x)

// 一元：Sleef 无标量版本，回退 libm
#define CLAC_SLEEF_U_fabs(x)       fabs(x)
#define CLAC_SLEEF_U_floor(x)      floor(x)
#define CLAC_SLEEF_U_ceil(x)       ceil(x)
#define CLAC_SLEEF_U_round(x)      round(x)
#define CLAC_SLEEF_U_trunc(x)      trunc(x)
#define CLAC_SLEEF_U_rint(x)       rint(x)
#define CLAC_SLEEF_U_nearbyint(x)  nearbyint(x)
#define CLAC_SLEEF_U_logb(x)       logb(x)

// 二元：Sleef 提供标量版本
#define CLAC_SLEEF_B_pow(x, y)      Sleef_pow_u10(x, y)
#define CLAC_SLEEF_B_atan2(x, y)    Sleef_atan2_u10(x, y)
#define CLAC_SLEEF_B_hypot(x, y)    Sleef_hypot_u35(x, y)

// 二元：Sleef 无标量版本，回退 libm
#define CLAC_SLEEF_B_copysign(x, y) copysign(x, y)
#define CLAC_SLEEF_B_fmod(x, y)     fmod(x, y)
#define CLAC_SLEEF_B_remainder(x, y) remainder(x, y)
#define CLAC_SLEEF_B_nextafter(x, y) nextafter(x, y)
#define CLAC_SLEEF_B_fmax(x, y)     fmax(x, y)
#define CLAC_SLEEF_B_fmin(x, y)     fmin(x, y)
#define CLAC_SLEEF_B_fdim(x, y)     fdim(x, y)
#else
#define MATH_FUNC_UNARY(func, x)   func(x)
#define MATH_FUNC_BINARY(func, x, y) func(x, y)
#endif

// ========== OpenMP 批量并行加速 ==========
#if defined(CLAC_USE_OPENMP)
  #include <omp.h>
  // 小数组的并行调度开销可能超过收益，故仅在元素数 >= 4096 时启用多线程。
  // 展开为 _Pragma，可直接置于各批量函数的 for 循环之前。
  #define CLAC_OMP_PARALLEL_FOR _Pragma("omp parallel for schedule(static) if(n >= 4096)")
#else
  #define CLAC_OMP_PARALLEL_FOR
#endif

/* ========== 可移植常量 ========== */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

/* ========== 手工 SIMD 特性检测（用于四则运算） ========== */
#if defined(__AVX__)
  #include <immintrin.h>
  #define CLAC_SIMD_AVX 1
#elif defined(__SSE2__)
  #include <emmintrin.h>
  #define CLAC_SIMD_SSE2 1
#endif

/* ========== Sleef 向量 API（AVX2，4 宽） ==========
 * 标量 Sleef 不会向量化且比 libm 慢；这里改用 Sleef 的 d4 向量函数，
 * 由编译器生成 AVX2 指令，一次处理 4 个 double。 */
#if defined(CLAC_USE_SLEEF) && defined(CLAC_SIMD_AVX)
  #define CLAC_SLEEF_VECTOR 1

  #define CLAC_SLEEF_V4_fabs(x)     _mm256_andnot_pd(_mm256_set1_pd(-0.0), (x))
  #define CLAC_SLEEF_V4_sqrt(x)     _mm256_sqrt_pd(x)   /* 硬件 sqrtpd，比 Sleef u35 更快且更精确 */
  #define CLAC_SLEEF_V4_cbrt(x)     Sleef_cbrtd4_u10avx2(x)
  #define CLAC_SLEEF_V4_exp(x)      Sleef_expd4_u10avx2(x)
  #define CLAC_SLEEF_V4_exp2(x)     Sleef_exp2d4_u10avx2(x)
  #define CLAC_SLEEF_V4_expm1(x)    Sleef_expm1d4_u10avx2(x)
  #define CLAC_SLEEF_V4_log(x)      Sleef_logd4_u10avx2(x)
  #define CLAC_SLEEF_V4_log10(x)    Sleef_log10d4_u10avx2(x)
  #define CLAC_SLEEF_V4_log2(x)     Sleef_log2d4_u10avx2(x)
  #define CLAC_SLEEF_V4_log1p(x)    Sleef_log1pd4_u10avx2(x)
  #define CLAC_SLEEF_V4_sin(x)      Sleef_sind4_u10avx2(x)
  #define CLAC_SLEEF_V4_cos(x)      Sleef_cosd4_u10avx2(x)
  #define CLAC_SLEEF_V4_tan(x)      Sleef_tand4_u10avx2(x)
  #define CLAC_SLEEF_V4_asin(x)     Sleef_asind4_u10avx2(x)
  #define CLAC_SLEEF_V4_acos(x)     Sleef_acosd4_u10avx2(x)
  #define CLAC_SLEEF_V4_atan(x)     Sleef_atand4_u10avx2(x)
  #define CLAC_SLEEF_V4_sinh(x)     Sleef_sinhd4_u10avx2(x)
  #define CLAC_SLEEF_V4_cosh(x)     Sleef_coshd4_u10avx2(x)
  #define CLAC_SLEEF_V4_tanh(x)     Sleef_tanhd4_u10avx2(x)
  #define CLAC_SLEEF_V4_asinh(x)    Sleef_asinhd4_u10avx2(x)
  #define CLAC_SLEEF_V4_acosh(x)    Sleef_acoshd4_u10avx2(x)
  #define CLAC_SLEEF_V4_atanh(x)    Sleef_atanhd4_u10avx2(x)
  #define CLAC_SLEEF_V4_erf(x)      Sleef_erfd4_u10avx2(x)
  #define CLAC_SLEEF_V4_erfc(x)     Sleef_erfcd4_u15avx2(x)
  #define CLAC_SLEEF_V4_tgamma(x)   Sleef_tgammad4_u10avx2(x)
  #define CLAC_SLEEF_V4_lgamma(x)   Sleef_lgammad4_u10avx2(x)

  #define CLAC_SLEEF_V4B_pow(x, y)    Sleef_powd4_u10avx2((x), (y))
  #define CLAC_SLEEF_V4B_atan2(x, y)  Sleef_atan2d4_u10avx2((x), (y))
  #define CLAC_SLEEF_V4B_hypot(x, y)  Sleef_hypotd4_u35avx2((x), (y))
#else
  #define CLAC_SLEEF_VECTOR 0
#endif

/* ========== 高质量伪随机数生成器：xoshiro256** ========== */
static uint64_t xoshiro_state[4];

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro_next(void) {
    const uint64_t result = rotl(xoshiro_state[1] * 5, 7) * 9;
    const uint64_t t = xoshiro_state[1] << 17;
    xoshiro_state[2] ^= xoshiro_state[0];
    xoshiro_state[3] ^= xoshiro_state[1];
    xoshiro_state[1] ^= xoshiro_state[2];
    xoshiro_state[0] ^= xoshiro_state[3];
    xoshiro_state[2] ^= t;
    xoshiro_state[3] = rotl(xoshiro_state[3], 45);
    return result;
}

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void xoshiro_seed(uint64_t seed) {
    xoshiro_state[0] = splitmix64(&seed);
    xoshiro_state[1] = splitmix64(&seed);
    xoshiro_state[2] = splitmix64(&seed);
    xoshiro_state[3] = splitmix64(&seed);
}

static double xoshiro_double(void) {
    return (xoshiro_next() >> 11) * 0x1.0p-53;
}

/* ========== Clac.Array 柔性数组定义 ========== */
#define CLAC_ARRAY_META "Clac.Array"

typedef struct {
    int len;
    double data[];   // C99 flexible array member
} ClacArray;

#define CLAC_ARRAY_SIZE(n) (sizeof(ClacArray) + (n) * sizeof(double))

// ---------- 构造与元方法 ----------
static int clac_array(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    if (n < 0) luaL_error(L, "size must be non-negative");
    ClacArray *arr = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(n), 0);
    arr->len = n;
    memset(arr->data, 0, n * sizeof(double));
    luaL_getmetatable(L, CLAC_ARRAY_META);
    lua_setmetatable(L, -2);
    return 1;
}

static int clac_array_index(lua_State *L) {
    ClacArray *arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 1 || idx > arr->len) return luaL_error(L, "index out of range");
    lua_pushnumber(L, arr->data[idx - 1]);
    return 1;
}

static int clac_array_newindex(lua_State *L) {
    ClacArray *arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 1 || idx > arr->len) return luaL_error(L, "index out of range");
    arr->data[idx - 1] = luaL_checknumber(L, 3);
    return 0;
}

static int clac_array_len(lua_State *L) {
    ClacArray *arr = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    lua_pushinteger(L, arr->len);
    return 1;
}

static int clac_array_gc(lua_State *L) {
    (void)L;   // 柔性数组自动回收
    return 0;
}

/* ========== 手工 SIMD 批量运算（加减乘除） ========== */
#define PREPARE_BATCH_RESULT(A, B) \
    if ((A)->len != (B)->len) return luaL_error(L, "size mismatch"); \
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE((A)->len), 0); \
    c->len = (A)->len; \
    luaL_getmetatable(L, CLAC_ARRAY_META); \
    lua_setmetatable(L, -2); \
    double* __restrict dc = c->data; \
    double* __restrict da = (A)->data; \
    double* __restrict db = (B)->data; \
    int n = (A)->len

static int clac_batch_add(lua_State *L) {
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray *b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META);
    PREPARE_BATCH_RESULT(a, b);
    #if CLAC_SIMD_AVX
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 4) {
            if (i + 3 < n) {
                __m256d va = _mm256_loadu_pd(da + i);
                __m256d vb = _mm256_loadu_pd(db + i);
                _mm256_storeu_pd(dc + i, _mm256_add_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] + db[j];
            }
        }
    #elif CLAC_SIMD_SSE2
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 2) {
            if (i + 1 < n) {
                __m128d va = _mm_loadu_pd(da + i);
                __m128d vb = _mm_loadu_pd(db + i);
                _mm_storeu_pd(dc + i, _mm_add_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] + db[j];
            }
        }
    #else
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i++) dc[i] = da[i] + db[i];
    #endif
    return 1;
}

static int clac_batch_sub(lua_State *L) {
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray *b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META);
    PREPARE_BATCH_RESULT(a, b);
    #if CLAC_SIMD_AVX
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 4) {
            if (i + 3 < n) {
                __m256d va = _mm256_loadu_pd(da + i);
                __m256d vb = _mm256_loadu_pd(db + i);
                _mm256_storeu_pd(dc + i, _mm256_sub_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] - db[j];
            }
        }
    #elif CLAC_SIMD_SSE2
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 2) {
            if (i + 1 < n) {
                __m128d va = _mm_loadu_pd(da + i);
                __m128d vb = _mm_loadu_pd(db + i);
                _mm_storeu_pd(dc + i, _mm_sub_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] - db[j];
            }
        }
    #else
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i++) dc[i] = da[i] - db[i];
    #endif
    return 1;
}

static int clac_batch_mul(lua_State *L) {
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray *b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META);
    PREPARE_BATCH_RESULT(a, b);
    #if CLAC_SIMD_AVX
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 4) {
            if (i + 3 < n) {
                __m256d va = _mm256_loadu_pd(da + i);
                __m256d vb = _mm256_loadu_pd(db + i);
                _mm256_storeu_pd(dc + i, _mm256_mul_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] * db[j];
            }
        }
    #elif CLAC_SIMD_SSE2
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 2) {
            if (i + 1 < n) {
                __m128d va = _mm_loadu_pd(da + i);
                __m128d vb = _mm_loadu_pd(db + i);
                _mm_storeu_pd(dc + i, _mm_mul_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] * db[j];
            }
        }
    #else
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i++) dc[i] = da[i] * db[i];
    #endif
    return 1;
}

static int clac_batch_div(lua_State *L) {
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray *b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META);
    PREPARE_BATCH_RESULT(a, b);
    #if CLAC_SIMD_AVX
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 4) {
            if (i + 3 < n) {
                __m256d va = _mm256_loadu_pd(da + i);
                __m256d vb = _mm256_loadu_pd(db + i);
                _mm256_storeu_pd(dc + i, _mm256_div_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] / db[j];
            }
        }
    #elif CLAC_SIMD_SSE2
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i += 2) {
            if (i + 1 < n) {
                __m128d va = _mm_loadu_pd(da + i);
                __m128d vb = _mm_loadu_pd(db + i);
                _mm_storeu_pd(dc + i, _mm_div_pd(va, vb));
            } else {
                for (int j = i; j < n; j++) dc[j] = da[j] / db[j];
            }
        }
    #else
        CLAC_OMP_PARALLEL_FOR
        for (int i = 0; i < n; i++) dc[i] = da[i] / db[i];
    #endif
    return 1;
}

/* ========== Sleef SIMD 批量一元函数 ========== */
#define BATCH_UNARY(name, func) \
static int clac_batch_##name(lua_State *L) { \
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META); \
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(a->len), 0); \
    c->len = a->len; \
    luaL_getmetatable(L, CLAC_ARRAY_META); \
    lua_setmetatable(L, -2); \
    double* __restrict da = a->data; \
    double* __restrict dc = c->data; \
    int n = a->len; \
    CLAC_OMP_PARALLEL_FOR \
    for (int i = 0; i < n; i++) { \
        dc[i] = MATH_FUNC_UNARY(func, da[i]); \
    } \
    return 1; \
}

/* ========== Sleef SIMD 批量二元函数 ========== */
#define BATCH_BINARY(name, func) \
static int clac_batch_##name(lua_State *L) { \
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META); \
    ClacArray *b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META); \
    if (a->len != b->len) return luaL_error(L, "size mismatch"); \
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(a->len), 0); \
    c->len = a->len; \
    luaL_getmetatable(L, CLAC_ARRAY_META); \
    lua_setmetatable(L, -2); \
    double* __restrict da = a->data; \
    double* __restrict db = b->data; \
    double* __restrict dc = c->data; \
    int n = a->len; \
    CLAC_OMP_PARALLEL_FOR \
    for (int i = 0; i < n; i++) { \
        dc[i] = MATH_FUNC_BINARY(func, da[i], db[i]); \
    } \
    return 1; \
}

/* ========== Sleef 向量批量函数（AVX2 4 宽，尾部标量回退） ========== */
#if CLAC_SLEEF_VECTOR
#define BATCH_UNARY_VEC(name, func) \
static int clac_batch_##name(lua_State *L) { \
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META); \
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(a->len), 0); \
    c->len = a->len; \
    luaL_getmetatable(L, CLAC_ARRAY_META); \
    lua_setmetatable(L, -2); \
    double* __restrict da = a->data; \
    double* __restrict dc = c->data; \
    int n = a->len; \
    CLAC_OMP_PARALLEL_FOR \
    for (int i = 0; i < n; i += 4) { \
        if (i + 3 < n) { \
            __m256d v = _mm256_loadu_pd(da + i); \
            _mm256_storeu_pd(dc + i, CLAC_SLEEF_V4_##func(v)); \
        } else { \
            for (int j = i; j < n; j++) dc[j] = MATH_FUNC_UNARY(func, da[j]); \
        } \
    } \
    return 1; \
}

#define BATCH_BINARY_VEC(name, func) \
static int clac_batch_##name(lua_State *L) { \
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META); \
    ClacArray *b = (ClacArray*)luaL_checkudata(L, 2, CLAC_ARRAY_META); \
    if (a->len != b->len) return luaL_error(L, "size mismatch"); \
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(a->len), 0); \
    c->len = a->len; \
    luaL_getmetatable(L, CLAC_ARRAY_META); \
    lua_setmetatable(L, -2); \
    double* __restrict da = a->data; \
    double* __restrict db = b->data; \
    double* __restrict dc = c->data; \
    int n = a->len; \
    CLAC_OMP_PARALLEL_FOR \
    for (int i = 0; i < n; i += 4) { \
        if (i + 3 < n) { \
            __m256d x = _mm256_loadu_pd(da + i); \
            __m256d y = _mm256_loadu_pd(db + i); \
            _mm256_storeu_pd(dc + i, CLAC_SLEEF_V4B_##func(x, y)); \
        } else { \
            for (int j = i; j < n; j++) dc[j] = MATH_FUNC_BINARY(func, da[j], db[j]); \
        } \
    } \
    return 1; \
}
#else
#define BATCH_UNARY_VEC(name, func)  BATCH_UNARY(name, func)
#define BATCH_BINARY_VEC(name, func) BATCH_BINARY(name, func)
#endif

/* ========== 辅助内联函数（用于无标准库函数的情形） ========== */
static inline double clac_deg_impl(double x) { return x * 180.0 / M_PI; }
static inline double clac_rad_impl(double x) { return x * M_PI / 180.0; }

/* ========== 生成全部一元批量函数 ========== */
BATCH_UNARY_VEC(abs,    fabs)
BATCH_UNARY(floor,  floor)
BATCH_UNARY(ceil,   ceil)
BATCH_UNARY(round,  round)
BATCH_UNARY(trunc,  trunc)
BATCH_UNARY(rint,   rint)
BATCH_UNARY(nearbyint, nearbyint)
BATCH_UNARY_VEC(sqrt,   sqrt)
BATCH_UNARY_VEC(cbrt,   cbrt)
BATCH_UNARY_VEC(exp,    exp)
BATCH_UNARY_VEC(exp2,   exp2)
BATCH_UNARY_VEC(expm1,  expm1)
BATCH_UNARY_VEC(log,    log)
BATCH_UNARY_VEC(log10,  log10)
BATCH_UNARY_VEC(log2,   log2)
BATCH_UNARY_VEC(log1p,  log1p)
BATCH_UNARY_VEC(sin,    sin)
BATCH_UNARY_VEC(cos,    cos)
BATCH_UNARY_VEC(tan,    tan)
BATCH_UNARY_VEC(asin,   asin)
BATCH_UNARY_VEC(acos,   acos)
BATCH_UNARY_VEC(atan,   atan)
BATCH_UNARY_VEC(sinh,   sinh)
BATCH_UNARY_VEC(cosh,   cosh)
BATCH_UNARY_VEC(tanh,   tanh)
BATCH_UNARY_VEC(asinh,  asinh)
BATCH_UNARY_VEC(acosh,  acosh)
BATCH_UNARY_VEC(atanh,  atanh)
BATCH_UNARY_VEC(erf,    erf)
BATCH_UNARY_VEC(erfc,   erfc)
BATCH_UNARY_VEC(tgamma, tgamma)
BATCH_UNARY_VEC(lgamma, lgamma)
BATCH_UNARY(logb,   logb)

/* ========== 角度转换（不使用 Sleef，保留内联） ========== */
static int clac_batch_deg(lua_State *L) {
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(a->len), 0);
    c->len = a->len;
    luaL_getmetatable(L, CLAC_ARRAY_META);
    lua_setmetatable(L, -2);
    double* __restrict da = a->data;
    double* __restrict dc = c->data;
    int n = a->len;
    CLAC_OMP_PARALLEL_FOR
    for (int i = 0; i < n; i++) {
        dc[i] = da[i] * 180.0 / M_PI;
    }
    return 1;
}

static int clac_batch_rad(lua_State *L) {
    ClacArray *a = (ClacArray*)luaL_checkudata(L, 1, CLAC_ARRAY_META);
    ClacArray *c = (ClacArray*)lua_newuserdatauv(L, CLAC_ARRAY_SIZE(a->len), 0);
    c->len = a->len;
    luaL_getmetatable(L, CLAC_ARRAY_META);
    lua_setmetatable(L, -2);
    double* __restrict da = a->data;
    double* __restrict dc = c->data;
    int n = a->len;
    CLAC_OMP_PARALLEL_FOR
    for (int i = 0; i < n; i++) {
        dc[i] = da[i] * M_PI / 180.0;
    }
    return 1;
}

/* ========== 生成全部二元批量函数（不含四则） ========== */
BATCH_BINARY_VEC(pow,       pow)
BATCH_BINARY_VEC(atan2,     atan2)
BATCH_BINARY_VEC(hypot,     hypot)
BATCH_BINARY(copysign,  copysign)
BATCH_BINARY(fmod,      fmod)
BATCH_BINARY(remainder, remainder)
BATCH_BINARY(nextafter, nextafter)
BATCH_BINARY(fmax,      fmax)
BATCH_BINARY(fmin,      fmin)
BATCH_BINARY(fdim,      fdim)

/* ========== 标量四则运算（保留原接口） ========== */
#define CLAC_OP(name, op) \
static int clac_##name(lua_State *L) { \
    lua_Number a = luaL_checknumber(L, 1); \
    lua_Number b = luaL_checknumber(L, 2); \
    lua_pushnumber(L, a op b); \
    return 1; \
}

CLAC_OP(add, +)
CLAC_OP(sub, -)
CLAC_OP(mul, *)
CLAC_OP(div, /)

/* ========== 取整与绝对值（标量） ========== */
static int clac_abs(lua_State *L)   { lua_pushnumber(L, fabs(luaL_checknumber(L, 1))); return 1; }
static int clac_floor(lua_State *L) { lua_pushnumber(L, floor(luaL_checknumber(L, 1))); return 1; }
static int clac_ceil(lua_State *L)  { lua_pushnumber(L, ceil(luaL_checknumber(L, 1))); return 1; }
static int clac_round(lua_State *L) { lua_pushnumber(L, round(luaL_checknumber(L, 1))); return 1; }
static int clac_trunc(lua_State *L) { lua_pushnumber(L, trunc(luaL_checknumber(L, 1))); return 1; }
static int clac_rint(lua_State *L)  { lua_pushnumber(L, rint(luaL_checknumber(L, 1))); return 1; }
static int clac_nearbyint(lua_State *L) { lua_pushnumber(L, nearbyint(luaL_checknumber(L, 1))); return 1; }
static int clac_lrint(lua_State *L) { lua_pushinteger(L, (lua_Integer)lrint(luaL_checknumber(L, 1))); return 1; }
static int clac_llrint(lua_State *L) { lua_pushinteger(L, (lua_Integer)llrint(luaL_checknumber(L, 1))); return 1; }

static int clac_min(lua_State *L) {
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

static int clac_max(lua_State *L) {
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

/* ========== 幂、指数、对数（标量） ========== */
static int clac_pow(lua_State *L)   { lua_pushnumber(L, pow(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_sqrt(lua_State *L)  { lua_pushnumber(L, sqrt(luaL_checknumber(L,1))); return 1; }
static int clac_cbrt(lua_State *L)  { lua_pushnumber(L, cbrt(luaL_checknumber(L,1))); return 1; }
static int clac_hypot(lua_State *L) { lua_pushnumber(L, hypot(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_exp(lua_State *L)   { lua_pushnumber(L, exp(luaL_checknumber(L,1))); return 1; }
static int clac_exp2(lua_State *L)  { lua_pushnumber(L, exp2(luaL_checknumber(L,1))); return 1; }
static int clac_expm1(lua_State *L) { lua_pushnumber(L, expm1(luaL_checknumber(L,1))); return 1; }

static int clac_log(lua_State *L) {
    lua_Number x = luaL_checknumber(L, 1);
    if (lua_gettop(L) >= 2) {
        lua_Number base = luaL_checknumber(L, 2);
        lua_pushnumber(L, log(x) / log(base));
    } else {
        lua_pushnumber(L, log(x));
    }
    return 1;
}
static int clac_log10(lua_State *L) { lua_pushnumber(L, log10(luaL_checknumber(L,1))); return 1; }
static int clac_log2(lua_State *L)  { lua_pushnumber(L, log2(luaL_checknumber(L,1))); return 1; }
static int clac_log1p(lua_State *L) { lua_pushnumber(L, log1p(luaL_checknumber(L,1))); return 1; }
static int clac_ilogb(lua_State *L) { lua_pushinteger(L, ilogb(luaL_checknumber(L, 1))); return 1; }
static int clac_logb(lua_State *L)  { lua_pushnumber(L, logb(luaL_checknumber(L,1))); return 1; }

/* ========== 三角函数（标量） ========== */
static int clac_sin(lua_State *L)  { lua_pushnumber(L, sin(luaL_checknumber(L,1))); return 1; }
static int clac_cos(lua_State *L)  { lua_pushnumber(L, cos(luaL_checknumber(L,1))); return 1; }
static int clac_tan(lua_State *L)  { lua_pushnumber(L, tan(luaL_checknumber(L,1))); return 1; }
static int clac_asin(lua_State *L) { lua_pushnumber(L, asin(luaL_checknumber(L,1))); return 1; }
static int clac_acos(lua_State *L) { lua_pushnumber(L, acos(luaL_checknumber(L,1))); return 1; }
static int clac_atan(lua_State *L) { lua_pushnumber(L, atan(luaL_checknumber(L,1))); return 1; }
static int clac_atan2(lua_State *L) { lua_pushnumber(L, atan2(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }

/* ========== 双曲函数（标量） ========== */
static int clac_sinh(lua_State *L)  { lua_pushnumber(L, sinh(luaL_checknumber(L,1))); return 1; }
static int clac_cosh(lua_State *L)  { lua_pushnumber(L, cosh(luaL_checknumber(L,1))); return 1; }
static int clac_tanh(lua_State *L)  { lua_pushnumber(L, tanh(luaL_checknumber(L,1))); return 1; }
static int clac_asinh(lua_State *L) { lua_pushnumber(L, asinh(luaL_checknumber(L,1))); return 1; }
static int clac_acosh(lua_State *L) { lua_pushnumber(L, acosh(luaL_checknumber(L,1))); return 1; }
static int clac_atanh(lua_State *L) { lua_pushnumber(L, atanh(luaL_checknumber(L,1))); return 1; }

/* ========== 特殊函数（标量） ========== */
static int clac_erf(lua_State *L)    { lua_pushnumber(L, erf(luaL_checknumber(L,1))); return 1; }
static int clac_erfc(lua_State *L)   { lua_pushnumber(L, erfc(luaL_checknumber(L,1))); return 1; }
static int clac_tgamma(lua_State *L) { lua_pushnumber(L, tgamma(luaL_checknumber(L,1))); return 1; }
static int clac_lgamma(lua_State *L) { lua_pushnumber(L, lgamma(luaL_checknumber(L,1))); return 1; }

/* ========== 浮点操作（标量） ========== */
static int clac_copysign(lua_State *L)   { lua_pushnumber(L, copysign(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_fmod(lua_State *L)       { lua_pushnumber(L, fmod(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_remainder(lua_State *L)  { lua_pushnumber(L, remainder(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_nextafter(lua_State *L)  { lua_pushnumber(L, nextafter(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_fma(lua_State *L)        { lua_pushnumber(L, fma(luaL_checknumber(L,1), luaL_checknumber(L,2), luaL_checknumber(L,3))); return 1; }
static int clac_fmax(lua_State *L)       { lua_pushnumber(L, fmax(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_fmin(lua_State *L)       { lua_pushnumber(L, fmin(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_fdim(lua_State *L)       { lua_pushnumber(L, fdim(luaL_checknumber(L,1), luaL_checknumber(L,2))); return 1; }
static int clac_nan(lua_State *L)        { lua_pushnumber(L, nan(luaL_optstring(L, 1, ""))); return 1; }
static int clac_modf(lua_State *L)       { double i; double f = modf(luaL_checknumber(L,1), &i); lua_pushnumber(L,i); lua_pushnumber(L,f); return 2; }
static int clac_frexp(lua_State *L)      { int e; double m = frexp(luaL_checknumber(L,1), &e); lua_pushnumber(L,m); lua_pushinteger(L,e); return 2; }
static int clac_ldexp(lua_State *L)      { lua_pushnumber(L, ldexp(luaL_checknumber(L,1), (int)luaL_checkinteger(L,2))); return 1; }
static int clac_scalbn(lua_State *L)     { lua_pushnumber(L, scalbn(luaL_checknumber(L,1), (int)luaL_checkinteger(L,2))); return 1; }
static int clac_remquo(lua_State *L)     { int q; double r = remquo(luaL_checknumber(L,1), luaL_checknumber(L,2), &q); lua_pushnumber(L,r); lua_pushinteger(L,q); return 2; }

/* ========== 分类函数（标量，返回 boolean） ========== */
static int clac_isfinite(lua_State *L)  { lua_pushboolean(L, isfinite(luaL_checknumber(L,1))); return 1; }
static int clac_isinf(lua_State *L)     { lua_pushboolean(L, isinf(luaL_checknumber(L,1))); return 1; }
static int clac_isnan(lua_State *L)     { lua_pushboolean(L, isnan(luaL_checknumber(L,1))); return 1; }
static int clac_isnormal(lua_State *L)  { lua_pushboolean(L, isnormal(luaL_checknumber(L,1))); return 1; }
static int clac_signbit(lua_State *L)   { lua_pushboolean(L, signbit(luaL_checknumber(L,1))); return 1; }

/* ========== 常量 ========== */
static int clac_pi(lua_State *L) { lua_pushnumber(L, M_PI); return 1; }
static int clac_e(lua_State *L)  { lua_pushnumber(L, M_E);  return 1; }

/* ========== 随机数 ========== */
static int clac_random(lua_State *L) {
    int n = lua_gettop(L);
    if (n == 0) {
        lua_pushnumber(L, xoshiro_double());
    } else if (n == 1) {
        lua_Integer max = luaL_checkinteger(L, 1);
        if (max < 1) return luaL_error(L, "max must be >= 1");
        lua_pushinteger(L, (lua_Integer)(xoshiro_next() % (uint64_t)max + 1));
    } else {
        lua_Integer min = luaL_checkinteger(L, 1);
        lua_Integer max = luaL_checkinteger(L, 2);
        if (min > max) return luaL_error(L, "min must be <= max");
        uint64_t range = (uint64_t)(max - min + 1);
        lua_pushinteger(L, (lua_Integer)(xoshiro_next() % range + min));
    }
    return 1;
}

static int clac_seed(lua_State *L) {
    xoshiro_seed((uint64_t)time(NULL));
    return 0;
}

/* ========== 角度转换（标量） ========== */
static int clac_deg(lua_State *L) { lua_pushnumber(L, luaL_checknumber(L,1) * 180.0 / M_PI); return 1; }
static int clac_rad(lua_State *L) { lua_pushnumber(L, luaL_checknumber(L,1) * M_PI / 180.0); return 1; }

/* ========== 注册表（完整，含所有批量函数） ========== */
static const luaL_Reg claclib[] = {
    // 数组构造
    {"array",        clac_array},

    // 手工 SIMD 批量四则
    {"batch_add",    clac_batch_add},
    {"batch_sub",    clac_batch_sub},
    {"batch_mul",    clac_batch_mul},
    {"batch_div",    clac_batch_div},

    // Sleef SIMD 一元批量函数
    {"batch_abs",    clac_batch_abs},
    {"batch_floor",  clac_batch_floor},
    {"batch_ceil",   clac_batch_ceil},
    {"batch_round",  clac_batch_round},
    {"batch_trunc",  clac_batch_trunc},
    {"batch_rint",   clac_batch_rint},
    {"batch_nearbyint", clac_batch_nearbyint},
    {"batch_sqrt",   clac_batch_sqrt},
    {"batch_cbrt",   clac_batch_cbrt},
    {"batch_exp",    clac_batch_exp},
    {"batch_exp2",   clac_batch_exp2},
    {"batch_expm1",  clac_batch_expm1},
    {"batch_log",    clac_batch_log},
    {"batch_log10",  clac_batch_log10},
    {"batch_log2",   clac_batch_log2},
    {"batch_log1p",  clac_batch_log1p},
    {"batch_sin",    clac_batch_sin},
    {"batch_cos",    clac_batch_cos},
    {"batch_tan",    clac_batch_tan},
    {"batch_asin",   clac_batch_asin},
    {"batch_acos",   clac_batch_acos},
    {"batch_atan",   clac_batch_atan},
    {"batch_sinh",   clac_batch_sinh},
    {"batch_cosh",   clac_batch_cosh},
    {"batch_tanh",   clac_batch_tanh},
    {"batch_asinh",  clac_batch_asinh},
    {"batch_acosh",  clac_batch_acosh},
    {"batch_atanh",  clac_batch_atanh},
    {"batch_erf",    clac_batch_erf},
    {"batch_erfc",   clac_batch_erfc},
    {"batch_tgamma", clac_batch_tgamma},
    {"batch_lgamma", clac_batch_lgamma},
    {"batch_logb",   clac_batch_logb},
    {"batch_deg",    clac_batch_deg},
    {"batch_rad",    clac_batch_rad},

    // Sleef SIMD 二元批量函数
    {"batch_pow",       clac_batch_pow},
    {"batch_atan2",     clac_batch_atan2},
    {"batch_hypot",     clac_batch_hypot},
    {"batch_copysign",  clac_batch_copysign},
    {"batch_fmod",      clac_batch_fmod},
    {"batch_remainder", clac_batch_remainder},
    {"batch_nextafter", clac_batch_nextafter},
    {"batch_fmax",      clac_batch_fmax},
    {"batch_fmin",      clac_batch_fmin},
    {"batch_fdim",      clac_batch_fdim},

    // 标量四则
    {"add",    clac_add},
    {"sub",    clac_sub},
    {"mul",    clac_mul},
    {"div",    clac_div},

    // 标量取整
    {"abs",    clac_abs},
    {"fabs",   clac_abs},
    {"floor",  clac_floor},
    {"ceil",   clac_ceil},
    {"round",  clac_round},
    {"trunc",  clac_trunc},
    {"rint",   clac_rint},
    {"nearbyint", clac_nearbyint},
    {"lrint",  clac_lrint},
    {"llrint", clac_llrint},

    // 标量极值
    {"min",    clac_min},
    {"max",    clac_max},
    {"fmax",   clac_fmax},
    {"fmin",   clac_fmin},

    // 标量幂指对
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
    {"ilogb",  clac_ilogb},
    {"logb",   clac_logb},

    // 标量三角
    {"sin",    clac_sin},
    {"cos",    clac_cos},
    {"tan",    clac_tan},
    {"asin",   clac_asin},
    {"acos",   clac_acos},
    {"atan",   clac_atan},
    {"atan2",  clac_atan2},

    // 标量双曲
    {"sinh",   clac_sinh},
    {"cosh",   clac_cosh},
    {"tanh",   clac_tanh},
    {"asinh",  clac_asinh},
    {"acosh",  clac_acosh},
    {"atanh",  clac_atanh},

    // 标量特殊
    {"erf",    clac_erf},
    {"erfc",   clac_erfc},
    {"tgamma", clac_tgamma},
    {"lgamma", clac_lgamma},

    // 标量浮点操作
    {"copysign",  clac_copysign},
    {"fmod",      clac_fmod},
    {"remainder", clac_remainder},
    {"nextafter", clac_nextafter},
    {"fma",       clac_fma},
    {"fdim",      clac_fdim},
    {"nan",       clac_nan},
    {"modf",      clac_modf},
    {"frexp",     clac_frexp},
    {"ldexp",     clac_ldexp},
    {"scalbn",    clac_scalbn},
    {"remquo",    clac_remquo},

    // 标量分类
    {"isfinite",  clac_isfinite},
    {"isinf",     clac_isinf},
    {"isnan",     clac_isnan},
    {"isnormal",  clac_isnormal},
    {"signbit",   clac_signbit},

    // 常量
    {"pi",     clac_pi},
    {"e",      clac_e},

    // 随机数
    {"random", clac_random},
    {"seed",   clac_seed},

    // 角度转换
    {"deg",    clac_deg},
    {"rad",    clac_rad},

    {NULL, NULL}
};

int luaopen_clac(lua_State *L) {
    luaL_newmetatable(L, CLAC_ARRAY_META);
    lua_pushstring(L, "__index");    lua_pushcfunction(L, clac_array_index);    lua_settable(L, -3);
    lua_pushstring(L, "__newindex"); lua_pushcfunction(L, clac_array_newindex); lua_settable(L, -3);
    lua_pushstring(L, "__len");      lua_pushcfunction(L, clac_array_len);      lua_settable(L, -3);
    lua_pushstring(L, "__gc");       lua_pushcfunction(L, clac_array_gc);       lua_settable(L, -3);
    lua_pop(L, 1);

    luaL_newlib(L, claclib);
    clac_seed(L);   // 初始化随机种子
    return 1;
}