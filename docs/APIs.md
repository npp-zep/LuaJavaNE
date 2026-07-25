# LuaJavaNE 内置模块文档

本文档介绍 LuaJavaNE 提供的三个核心 C 扩展模块：`clac`（高性能数学计算）、`utils`（高精度时间工具）和 `gc`（Java 对象引用管理）。

---

## 目录

1. [clac —— 高性能数学计算库](#clac--高性能数学计算库)
   - [标量函数](#标量函数)
   - [批量数组运算](#批量数组运算)
   - [常量](#常量)
   - [随机数](#随机数)
2. [utils —— 高精度时间工具库](#utils--高精度时间工具库)
   - [时间函数](#时间函数)
   - [休眠函数](#休眠函数)
   - [计时器](#计时器)
3. [gc —— Java 对象引用管理](#gc--java-对象引用管理)
   - [引用类型](#引用类型)
   - [引用操作](#引用操作)

---

## clac —— 高性能数学计算库

`clac` 是一个完整的 C99 `<math.h>` 包装库，支持标量运算和批量数组运算。批量运算使用 **Sleef SIMD 库** 进行加速，在支持的 CPU 上可获得 **83 倍以上的性能提升**。

### 加载模块

~~~lua
local clac = require("clac")
~~~

---

### 标量函数

#### 基础四则运算

| 函数 | 说明 | 示例 |
|------|------|------|
| `clac.add(a, b)` | 加法 | `clac.add(3, 5)` → `8` |
| `clac.sub(a, b)` | 减法 | `clac.sub(10, 3)` → `7` |
| `clac.mul(a, b)` | 乘法 | `clac.mul(4, 6)` → `24` |
| `clac.div(a, b)` | 除法 | `clac.div(15, 3)` → `5` |

#### 取整与绝对值

| 函数 | 说明 | 示例 |
|------|------|------|
| `clac.abs(x)` / `clac.fabs(x)` | 绝对值 | `clac.abs(-3.14)` → `3.14` |
| `clac.floor(x)` | 向下取整 | `clac.floor(3.9)` → `3` |
| `clac.ceil(x)` | 向上取整 | `clac.ceil(3.1)` → `4` |
| `clac.round(x)` | 四舍五入 | `clac.round(3.5)` → `4` |
| `clac.trunc(x)` | 截断取整 | `clac.trunc(3.9)` → `3` |
| `clac.rint(x)` | 四舍五入（银行家舍入） | `clac.rint(2.5)` → `2` |
| `clac.nearbyint(x)` | 四舍五入（不引发异常） | `clac.nearbyint(3.5)` → `4` |
| `clac.lrint(x)` | 四舍五入为整数 | `clac.lrint(3.14)` → `3` |
| `clac.llrint(x)` | 四舍五入为长整数 | `clac.llrint(3.14)` → `3` |

#### 极值

| 函数 | 说明 | 示例 |
|------|------|------|
| `clac.min(x, y, ...)` | 返回最小值 | `clac.min(3, 1, 5)` → `1` |
| `clac.max(x, y, ...)` | 返回最大值 | `clac.max(3, 1, 5)` → `5` |
| `clac.fmax(x, y)` | 浮点数最大值 | `clac.fmax(3.1, 2.9)` → `3.1` |
| `clac.fmin(x, y)` | 浮点数最小值 | `clac.fmin(3.1, 2.9)` → `2.9` |

#### 幂、指数、对数

| 函数 | 说明 | 示例 |
|------|------|------|
| `clac.pow(x, y)` | x 的 y 次幂 | `clac.pow(2, 3)` → `8` |
| `clac.sqrt(x)` | 平方根 | `clac.sqrt(16)` → `4` |
| `clac.cbrt(x)` | 立方根 | `clac.cbrt(27)` → `3` |
| `clac.hypot(x, y)` | 欧几里得距离 | `clac.hypot(3, 4)` → `5` |
| `clac.exp(x)` | e 的 x 次幂 | `clac.exp(1)` → `2.71828` |
| `clac.exp2(x)` | 2 的 x 次幂 | `clac.exp2(3)` → `8` |
| `clac.expm1(x)` | e^x - 1 | `clac.expm1(1e-8)` → `1e-8` |
| `clac.log(x)` | 自然对数 | `clac.log(2.71828)` → `1` |
| `clac.log(x, base)` | 指定底数的对数 | `clac.log(8, 2)` → `3` |
| `clac.log10(x)` | 常用对数 | `clac.log10(100)` → `2` |
| `clac.log2(x)` | 以 2 为底的对数 | `clac.log2(8)` → `3` |
| `clac.log1p(x)` | log(1 + x) | `clac.log1p(1e-8)` → `1e-8` |
| `clac.ilogb(x)` | 提取指数（整数） | `clac.ilogb(8)` → `3` |
| `clac.logb(x)` | 提取指数（浮点数） | `clac.logb(8)` → `3` |

#### 三角函数

| 函数 | 说明 | 示例 |
|------|------|------|
| `clac.sin(x)` | 正弦（弧度） | `clac.sin(1.57)` → `0.99999968` |
| `clac.cos(x)` | 余弦（弧度） | `clac.cos(0)` → `1` |
| `clac.tan(x)` | 正切（弧度） | `clac.tan(0.7854)` → `1` |
| `clac.asin(x)` | 反正弦 | `clac.asin(1)` → `1.5708` |
| `clac.acos(x)` | 反余弦 | `clac.acos(0)` → `1.5708` |
| `clac.atan(x)` | 反正切 | `clac.atan(1)` → `0.7854` |
| `clac.atan2(y, x)` | 带象限的反正切 | `clac.atan2(1, 1)` → `0.7854` |

#### 双曲函数

| 函数 | 说明 |
|------|------|
| `clac.sinh(x)` | 双曲正弦 |
| `clac.cosh(x)` | 双曲余弦 |
| `clac.tanh(x)` | 双曲正切 |
| `clac.asinh(x)` | 反双曲正弦 |
| `clac.acosh(x)` | 反双曲余弦 |
| `clac.atanh(x)` | 反双曲正切 |

#### 特殊函数

| 函数 | 说明 |
|------|------|
| `clac.erf(x)` | 误差函数 |
| `clac.erfc(x)` | 互补误差函数 |
| `clac.tgamma(x)` | Gamma 函数 |
| `clac.lgamma(x)` | Gamma 函数的自然对数 |

#### 浮点操作

| 函数 | 说明 |
|------|------|
| `clac.copysign(x, y)` | 复制 y 的符号到 x |
| `clac.fmod(x, y)` | 浮点数取模 |
| `clac.remainder(x, y)` | IEEE 754 余数 |
| `clac.nextafter(x, y)` | 向 y 方向的下一个可表示数 |
| `clac.fma(x, y, z)` | 融合乘加 (x * y + z) |
| `clac.fdim(x, y)` | 正差 (max(x-y, 0)) |
| `clac.nan(tag)` | 生成 NaN |
| `clac.modf(x)` | 分解为整数和小数部分 |
| `clac.frexp(x)` | 分解为尾数和指数 |
| `clac.ldexp(x, exp)` | 乘以 2 的 exp 次幂 |
| `clac.scalbn(x, exp)` | 乘以 FLT_RADIX 的 exp 次幂 |
| `clac.remquo(x, y)` | 余数并返回商 |

#### 分类函数

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `clac.isfinite(x)` | 是否为有限数 | boolean |
| `clac.isinf(x)` | 是否为无穷大 | boolean |
| `clac.isnan(x)` | 是否为 NaN | boolean |
| `clac.isnormal(x)` | 是否为正规数 | boolean |
| `clac.signbit(x)` | 符号位是否为负 | boolean |

#### 角度转换

| 函数 | 说明 | 示例 |
|------|------|------|
| `clac.deg(x)` | 弧度转角度 | `clac.deg(3.14159)` → `180` |
| `clac.rad(x)` | 角度转弧度 | `clac.rad(180)` → `3.14159` |

---

### 批量数组运算

`clac` 提供了 `ClacArray` 类型，用于在 C 内存中存储连续的双精度浮点数数组，支持 SIMD 加速的批量运算。

#### 创建数组

~~~lua
-- 创建指定大小的数组（初始化为 0）
local a = clac.array(10000)

-- 访问和修改元素
a[1] = 3.14
print(a[1])  -- 3.14

-- 获取数组长度
print(#a)    -- 10000
~~~

#### 批量运算函数

所有批量函数都返回一个新的 `ClacArray`，原数组不变。

**四则运算（手工 SIMD 优化）**

| 函数 | 说明 |
|------|------|
| `clac.batch_add(a, b)` | 逐元素加法 |
| `clac.batch_sub(a, b)` | 逐元素减法 |
| `clac.batch_mul(a, b)` | 逐元素乘法 |
| `clac.batch_div(a, b)` | 逐元素除法 |

**一元函数（Sleef SIMD 加速）**

~~~lua
clac.batch_abs(a)       -- 绝对值
clac.batch_floor(a)     -- 向下取整
clac.batch_ceil(a)      -- 向上取整
clac.batch_round(a)     -- 四舍五入
clac.batch_trunc(a)     -- 截断
clac.batch_rint(a)      -- 银行家舍入
clac.batch_nearbyint(a) -- 四舍五入
clac.batch_sqrt(a)      -- 平方根
clac.batch_cbrt(a)      -- 立方根
clac.batch_exp(a)       -- 指数
clac.batch_exp2(a)      -- 2 的幂
clac.batch_expm1(a)     -- e^x - 1
clac.batch_log(a)       -- 自然对数
clac.batch_log10(a)     -- 常用对数
clac.batch_log2(a)      -- 以 2 为底的对数
clac.batch_log1p(a)     -- log(1 + x)
clac.batch_sin(a)       -- 正弦
clac.batch_cos(a)       -- 余弦
clac.batch_tan(a)       -- 正切
clac.batch_asin(a)      -- 反正弦
clac.batch_acos(a)      -- 反余弦
clac.batch_atan(a)      -- 反正切
clac.batch_sinh(a)      -- 双曲正弦
clac.batch_cosh(a)      -- 双曲余弦
clac.batch_tanh(a)      -- 双曲正切
clac.batch_asinh(a)     -- 反双曲正弦
clac.batch_acosh(a)     -- 反双曲余弦
clac.batch_atanh(a)     -- 反双曲正切
clac.batch_erf(a)       -- 误差函数
clac.batch_erfc(a)      -- 互补误差函数
clac.batch_tgamma(a)    -- Gamma 函数
clac.batch_lgamma(a)    -- 对数 Gamma
clac.batch_logb(a)      -- 提取指数
clac.batch_deg(a)       -- 弧度转角度
clac.batch_rad(a)       -- 角度转弧度
~~~

**二元函数（Sleef SIMD 加速）**

~~~lua
clac.batch_pow(a, b)       -- 幂运算
clac.batch_atan2(a, b)     -- 带象限的反正切
clac.batch_hypot(a, b)     -- 欧几里得距离
clac.batch_copysign(a, b)  -- 复制符号
clac.batch_fmod(a, b)      -- 取模
clac.batch_remainder(a, b) -- IEEE 754 余数
clac.batch_nextafter(a, b) -- 下一个可表示数
clac.batch_fmax(a, b)      -- 逐元素最大值
clac.batch_fmin(a, b)      -- 逐元素最小值
clac.batch_fdim(a, b)      -- 逐元素正差
~~~

#### 性能示例

~~~lua
local clac = require("clac")

-- 创建两个大数组
local size = 1000000
local a = clac.array(size)
local b = clac.array(size)

-- 填充数据
for i = 1, size do
    a[i] = math.sin(i)
    b[i] = math.cos(i)
end

-- 批量运算（SIMD 加速，比 Lua 循环快 83 倍）
local c = clac.batch_add(a, b)
local d = clac.batch_sin(c)
~~~

---

### 常量

| 函数 | 说明 | 值 |
|------|------|-----|
| `clac.pi()` | 圆周率 π | 3.1415926535898 |
| `clac.e()` | 自然常数 e | 2.7182818284590 |

---

### 随机数

`clac` 使用 **xoshiro256** 高质量伪随机数生成器。

~~~lua
-- 初始化种子（使用当前时间）
clac.seed()

-- 生成 [0, 1) 之间的随机浮点数
local r = clac.random()

-- 生成 [1, max] 之间的随机整数
local n = clac.random(100)

-- 生成 [min, max] 之间的随机整数
local m = clac.random(10, 20)
~~~

---

## utils —— 高精度时间工具库

`utils` 提供跨平台的高精度时间测量和休眠功能，基于 `clock_gettime`（POSIX）和 `QueryPerformanceCounter`（Windows）实现纳秒级精度。

### 加载模块

~~~lua
local utils = require("utils")
~~~

---

### 时间函数

| 函数 | 说明 | 精度 | 示例输出 |
|------|------|------|----------|
| `utils.ns_time()` | 当前时间（秒，浮点数） | 纳秒级 | `1784972795.252` |
| `utils.monotonic_time()` | 单调时间（不受系统时间影响） | 纳秒级 | `16979.922325201` |
| `utils.timestamp()` | Unix 时间戳（秒，整数） | 秒级 | `1784972795` |
| `utils.timestamp_ms()` | Unix 时间戳（毫秒，整数） | 毫秒级 | `1784972795252` |

**使用示例：**

~~~lua
local utils = require("utils")

-- 测量代码执行时间
local start = utils.ns_time()
-- ... 执行操作 ...
local elapsed = utils.ns_time() - start
print(string.format("耗时: %.6f 秒", elapsed))

-- 获取当前时间戳
local ts = utils.timestamp()
local ts_ms = utils.timestamp_ms()
print(ts, ts_ms)
~~~

---

### 休眠函数

| 函数 | 说明 | 精度 |
|------|------|------|
| `utils.sleep(sec)` | 休眠指定秒数（支持小数） | 微秒级 |
| `utils.sleep_ms(ms)` | 休眠指定毫秒数 | 毫秒级 |
| `utils.sleep_us(us)` | 休眠指定微秒数 | 微秒级 |
| `utils.sleep_ns(ns)` | 休眠指定纳秒数 | 纳秒级（受系统调度限制） |

**使用示例：**

~~~lua
local utils = require("utils")

-- 休眠 0.5 秒
utils.sleep(0.5)

-- 休眠 100 毫秒
utils.sleep_ms(100)

-- 休眠 1000 微秒（1 毫秒）
utils.sleep_us(1000)

-- 休眠 1,000,000 纳秒（1 毫秒）
utils.sleep_ns(1000000)
~~~

**精度说明：**

- **POSIX 系统**（Linux、macOS、Android）：使用 `nanosleep`，理论精度纳秒级，实际受内核调度影响（通常 1-10ms）
- **Windows**：使用 `WaitableTimer` + 忙等待混合策略，短休眠（<100μs）使用忙等待保持精度

---

### 计时器

`utils.timer()` 创建一个高性能计时器对象，用于精确测量时间间隔。

**创建计时器：**

~~~lua
local timer = utils.timer()
~~~

**方法：**

| 方法 | 说明 | 返回值 |
|------|------|--------|
| `timer:elapsed()` | 从创建或最后一次 reset 到现在的总时间 | 秒（浮点数） |
| `timer:lap()` | 从上一次 lap 到现在的时间，并自动更新 | 秒（浮点数） |
| `timer:reset()` | 重置计时器到当前时间 | 无 |

**使用示例：**

~~~lua
local timer = utils.timer()

-- 模拟一些工作
utils.sleep_ms(100)
print(timer:elapsed())  -- ~0.101 秒

utils.sleep_ms(50)
print(timer:lap())      -- ~0.052 秒

utils.sleep_ms(30)
print(timer:lap())      -- ~0.031 秒

-- 重置计时器
timer:reset()
utils.sleep_ms(20)
print(timer:elapsed())  -- ~0.020 秒
~~~

**实际应用：帧率控制**

~~~lua
local function run_at_fps(fps, update, stop_condition)
    local interval = 1.0 / fps
    local timer = utils.timer()
    
    while not stop_condition() do
        update()
        local elapsed = timer:lap()
        if elapsed < interval then
            utils.sleep(interval - elapsed)
        end
    end
end

-- 以 60 FPS 运行
local running = true
run_at_fps(60, 
    function() print("frame") end,
    function() return not running end
)
~~~

**实际应用：带超时的轮询**

~~~lua
local function wait_for_condition(check, timeout_sec)
    local start = utils.ns_time()
    while utils.ns_time() - start < timeout_sec do
        if check() then return true end
        utils.sleep_ms(1)  -- 避免忙等
    end
    return false
end

-- 等待某个条件成立，超时 5 秒
local ok = wait_for_condition(
    function() return some_condition() end,
    5
)
~~~

---

## gc —— Java 对象引用管理

`gc` 模块提供对 Java 对象引用的显式管理，支持强引用和弱引用。这在需要跨 Lua 状态保持 Java 对象时非常有用。

### 加载模块

~~~lua
local gc = require("gc")
~~~

---

### 引用类型

| 类型 | 说明 | 行为 |
|------|------|------|
| **强引用** | 阻止 Java GC 回收对象 | 直到显式释放或 Lua 状态结束 |
| **弱引用** | 不阻止 Java GC 回收对象 | 当 Java 对象被 GC 回收后，引用变为无效 |

---

### 引用操作

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `gc.hold(obj)` | 创建对象的强引用 | 引用 ID（整数） |
| `gc.holdWeak(obj)` | 创建对象的弱引用 | 引用 ID（整数） |
| `gc.get(id)` | 根据 ID 获取对象 | Java 对象或 nil |
| `gc.release(id)` | 释放引用 | boolean（是否成功） |
| `gc.exists(id)` | 检查引用是否存在且有效 | boolean |
| `gc.count()` | 获取当前引用总数 | 整数 |
| `gc.list()` | 列出所有引用 ID | 表（数组） |
| `gc.clear()` | 清除所有引用 | 无 |

---

### 使用示例

**基本用法：**

~~~lua
local java = require("java")
local gc = require("gc")

-- 创建一个 Java 对象
local String = java.import("java.lang.String")
local s = String:new("Hello World")

-- 创建强引用
local ref = gc.hold(s)
print(ref)  -- 1

-- 通过引用获取对象
local obj = gc.get(ref)
print(obj:length())  -- 11

-- 检查引用是否存在
print(gc.exists(ref))  -- true

-- 列出所有引用
for _, id in ipairs(gc.list()) do
    print("引用 ID:", id)
end

-- 释放引用
gc.release(ref)
print(gc.exists(ref))  -- false
~~~

**弱引用示例：**

~~~lua
local java = require("java")
local gc = require("gc")

local String = java.import("java.lang.String")
local s = String:new("Temporary")

-- 创建弱引用
local weakRef = gc.holdWeak(s)

-- 主动触发 GC（注意：实际使用中不推荐频繁触发）
java.import("java.lang.System"):gc()

-- 检查对象是否还存活
if gc.exists(weakRef) then
    local obj = gc.get(weakRef)
    print("对象仍然存在:", obj)
else
    print("对象已被回收")
end

-- 释放弱引用
gc.release(weakRef)
~~~

**跨 Lua 状态保持对象：**

~~~lua
-- 场景：在多个 Lua 脚本之间共享 Java 对象

-- script1.lua
local java = require("java")
local gc = require("gc")

local HashMap = java.import("java.util.HashMap")
local map = HashMap:new()
map:put("key", "value")

-- 存储引用（将引用 ID 保存到文件或传递给其他脚本）
local ref = gc.hold(map)
-- 保存 ref 到文件或全局变量...

-- script2.lua
local gc = require("gc")

-- 从文件或全局变量读取 ref
local ref = 1  -- 假设从外部获取
local map = gc.get(ref)
if map then
    print(map:get("key"))  -- "value"
end

-- 使用完毕后释放
gc.release(ref)
~~~

**引用统计与清理：**

~~~lua
local gc = require("gc")

-- 获取当前引用数量
local count = gc.count()
print("当前引用数:", count)

-- 列出所有引用 ID
local ids = gc.list()
for i, id in ipairs(ids) do
    print(i, ":", id)
end

-- 清理所有引用（谨慎使用）
gc.clear()
print("清理后引用数:", gc.count())
~~~

---

### 使用场景

1. **跨 Lua 状态共享 Java 对象**：在多个 `LuaRuntime` 实例之间传递 Java 对象
2. **防止 Java 对象被提前回收**：当对象需要跨多次 Lua 调用存活时
3. **弱引用缓存**：实现缓存机制，当内存紧张时允许对象被回收
4. **调试引用泄漏**：使用 `gc.list()` 和 `gc.count()` 监控引用情况

---

## 性能说明

| 模块 | 性能特点 |
|------|----------|
| **clac** | 标量函数与标准 C 库相当；批量运算使用 SIMD（AVX/SSE），比纯 Lua 循环快 10-83 倍 |
| **utils** | 时间函数纳秒级精度，系统调用开销约 100ns；休眠函数精度受系统调度限制 |
| **gc** | 引用操作使用哈希表存储，O(1) 查找；弱引用使用 JNI 弱全局引用，无额外 GC 开销 |

---

## 跨平台支持

| 平台 | clac | utils | gc |
|------|------|-------|-----|
| Linux (x86_64) | ✅ SIMD (AVX/SSE) | ✅ clock_gettime | ✅ |
| Linux (ARM64) | ✅ SIMD (NEON) | ✅ clock_gettime | ✅ |
| macOS (x86_64) | ✅ SIMD (AVX/SSE) | ✅ clock_gettime | ✅ |
| macOS (ARM64) | ✅ SIMD (NEON) | ✅ clock_gettime | ✅ |
| Windows (x86_64) | ✅ SIMD (AVX/SSE) | ✅ QPC | ✅ |
| Android (Termux) | ✅ SIMD (NEON) | ✅ clock_gettime | ✅ |

---

## 相关链接

- [LuaJavaNE GitHub](https://github.com/npp-zep/LuaJavaNE)
- [Lua 5.4 参考手册](https://www.lua.org/manual/5.4/)
- [Sleef SIMD 库](https://sleef.org/)