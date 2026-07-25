# LuaJavaNE

![Lua](https://img.shields.io/badge/Lua-5.4.8-blue) ![Java](https://img.shields.io/badge/Java-17%2B-orange) ![License](https://img.shields.io/badge/license-MIT-green)

**Lua 5.4.8 + Java 双向互调引擎**。在 Lua 里直接调用任何 Java 类库，在 Java 里执行 Lua 脚本。  
支持异步多线程、批量高性能数学运算，架构清晰，升级 Lua 仅需替换目录。

---

## 项目结构

~~~
LuaJavaNE/
├── lua/                      # Lua 官方源码（解压即用，无需任何修改）
│   └── lua-5.4.8/
├── native/                   # C 代码
│   ├── jni/                  # JNI 桥接
│   └── lualib/               # 自定义 Lua 库（java / clac / async / utils / gc）
├── java/src/com/luajava/     # Java 源码
├── test/                     # JUnit 测试
├── lib/                      # 第三方 jar
├── docs/                     # 文档
├── examples/                 # 示例脚本
├── CMakeLists.txt            # C 构建配置
├── Makefile                  # 主构建文件
└── luaj.sh                   # 启动脚本
~~~

---

## 快速开始

### 从源码构建

~~~bash
git clone https://github.com/npp-zep/LuaJavaNE.git
cd LuaJavaNE
make          # 完整构建（C + Java）
# 或者使用 Ninja 加速 C 编译：
make ninja    # 需要安装 ninja-build
~~~

构建完成后运行 REPL：

~~~bash
./luaj.sh
~~~

### 下载 Release

从 Releases 下载 luajava.jar 和 luajava.so，放在同一目录：

~~~bash
java -Dluajava.library.path=./luajava.so -jar luajava.jar
~~~

---

## 升级 Lua 版本

本项目 Lua 源码完全保持原样，不进行任何修改。升级到新版本只需两步：

1. 删除旧版本目录：
   ~~~bash
   rm -rf lua/lua-5.4.8
   ~~~

2. 下载新版本并解压到 lua/ 目录：
   ~~~bash
   wget https://lua.org/ftp/lua-5.5.0.tar.gz
   tar xzf lua-5.5.0.tar.gz -C lua/
   ~~~

构建系统会自动识别 lua/lua-* 目录，无需改动任何代码或配置。

---

## 作为库使用

### Java 调用 Lua

~~~java
LuaRuntime L = new LuaRuntime();
L.doString("function add(a, b) return a + b end");
Object result = L.callFunction("add", 3, 5); // 8

LuaFunctionObj fn = L.compile("return function(x) return x * 2 end");
LuaFunctionObj doubler = (LuaFunctionObj) fn.call();
doubler.call(21); // 42
fn.destroy(); doubler.destroy();
L.close();
~~~

### Lua 调用 Java

~~~lua
local java = require("java")
local String = java.import("java.lang.String")
local s = String:new("Hello World")
print(s:length())          -- 11
print(s:substring(0, 5))   -- Hello
~~~

### 注解绑定

~~~java
@LuaModule("math")
class MyMath {
    @LuaFunction public int add(int a, int b) { return a + b; }
    @LuaFunction("multiply") public int mul(int a, int b) { return a * b; }
}
~~~

~~~lua
LuaRuntime L = new LuaRuntime();
L.registerModule(new MyMath());
L.doString("print(math_add(3, 5))");      -- 8
L.doString("print(math_multiply(6, 7))"); -- 42
~~~

### 动态代理

用 Lua 表实现 Java 接口：

~~~lua
local Runnable = java.import("java.lang.Runnable")
local handler = { run = function(self) print("Hello from Lua!") end }
local proxy = java.createProxy({"java.lang.Runnable"}, handler)
java.import("java.lang.Thread"):new(proxy):start()
~~~

---

## Agent v2 异步 API（多线程）

Agent v2 提供强大的异步执行能力，所有任务在后台线程池中运行，结果通过 Promise 机制回传主线程。

### 静态方法异步调用

~~~lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
repeat local done, result = java.checkPromise(id) until done
print(result)  -- 42
~~~

### 实例方法异步调用

~~~lua
local String = java.import("java.lang.String")
local s = String:new("Hello World")
local id = java.promise()
java.runAsyncObj(id, s, "length")
repeat local done, result = java.checkPromise(id) until done
print(result)  -- 11
~~~

### 异步构造对象

~~~lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello")
repeat local done, oid = java.checkPromise(id) until done
local obj = java.getObject(oid)   -- 获取 userdata
print(obj:length())               -- 5
~~~

### 多返回值

~~~lua
local s = String:new("a,b,c")
local id = java.promise()
java.runAsyncObj(id, s, "split", ",")
repeat local done, a, b, c = java.checkPromise(id) until done
print(a, b, c)  -- a   b   c
~~~

### 错误处理

异步任务中抛出的异常会被捕获并作为字符串返回：

~~~lua
java.runAsync(id, "java.lang.NonExistent", "foo", "")
-- checkPromise 返回: "java.lang.ClassNotFoundException: java.lang.NonExistent"
~~~

---

## Clac 高性能数学库

clac 模块提供了完整的 C 标准数学函数，以及批量数组运算（83 倍加速）。

### 基本用法

~~~lua
local clac = require("clac")
print(clac.pi())            -- 3.1415926535898
print(clac.sin(1.57))       -- 0.99999968293183
print(clac.erf(1.0))        -- 0.84270079294971
print(clac.tgamma(5.0))     -- 24.0
~~~

### 批量运算（ClacArray）

~~~lua
local a = clac.array(10000)
local b = clac.array(10000)
-- 填充数据...
local c = clac.batch_add(a, b)  -- 直接在 C 内存中完成，比 Lua 表循环快 83 倍
local d = clac.batch_sin(a)     -- 逐元素求正弦
~~~

### 支持的批量函数

| 分类 | 函数 |
|------|------|
| 四则运算 | batch_add, batch_sub, batch_mul, batch_div |
| 三角函数 | batch_sin, batch_cos, batch_tan, batch_asin, batch_acos, batch_atan |
| 双曲函数 | batch_sinh, batch_cosh, batch_tanh, batch_asinh, batch_acosh, batch_atanh |
| 指数对数 | batch_exp, batch_exp2, batch_expm1, batch_log, batch_log10, batch_log2, batch_log1p |
| 取整 | batch_floor, batch_ceil, batch_round, batch_trunc, batch_rint |
| 特殊函数 | batch_erf, batch_erfc, batch_tgamma, batch_lgamma |
| 其他 | batch_sqrt, batch_cbrt, batch_pow, batch_atan2, batch_hypot |

---

## Utils 高精度工具库

utils 模块提供高精度时间和休眠功能，适用于性能测量和精确控制。

### 时间函数

~~~lua
local utils = require("utils")

-- 高精度时间（秒，浮点数，纳秒级精度）
local t = utils.ns_time()

-- 单调时间（不受系统时间调整影响）
local mt = utils.monotonic_time()

-- Unix 时间戳（秒，整数）
local ts = utils.timestamp()

-- Unix 时间戳（毫秒，整数）
local ts_ms = utils.timestamp_ms()
~~~

### 休眠函数

~~~lua
-- 休眠指定秒数（支持小数）
utils.sleep(0.5)      -- 500ms

-- 休眠毫秒
utils.sleep_ms(100)

-- 休眠微秒
utils.sleep_us(1000)  -- 1ms

-- 休眠纳秒
utils.sleep_ns(1000000)  -- 1ms
~~~

### 计时器

~~~lua
local timer = utils.timer()

-- 获取从开始到现在的总时间
local elapsed = timer:elapsed()

-- 获取从上一次 lap 到现在的时间
local lap = timer:lap()

-- 重置计时器
timer:reset()
~~~

### 性能基准示例

~~~lua
local function benchmark_sleep(count, duration_ms)
    local t = utils.timer()
    for i = 1, count do
        utils.sleep_ms(duration_ms)
    end
    local total = t:elapsed()
    local expected = count * duration_ms / 1000.0
    local error_pct = (total - expected) / expected * 100
    return total, error_pct
end

local total, err = benchmark_sleep(10, 10)
print(string.format("10次*10ms: 实际=%.3fs, 误差=%.1f%%", total, err))
~~~

---

## GC 弱引用管理

gc 模块提供跨语言对象引用管理，支持强引用和弱引用。

~~~lua
local gc = require("gc")
local String = java.import("java.lang.String")
local s = String:new("Hello")

-- 创建强引用
local ref = gc.hold(s)

-- 获取引用对象
local obj = gc.get(ref)  -- 返回 userdata

-- 检查引用是否存在
if gc.exists(ref) then
    print("Object is still alive")
end

-- 释放引用
gc.release(ref)

-- 弱引用（不阻止 GC）
local weak_ref = gc.holdWeak(s)
~~~

---

## 命令行

~~~bash
luaj                    # 启动交互式 REPL
luaj script.lua         # 执行脚本文件
luaj -e "print(1+1)"    # 执行一行代码
luaj -v                 # 版本信息（含编译器版本）
luaj -h                 # 帮助
~~~

### REPL 快捷键

| 快捷键 | 功能 |
|--------|------|
| ↑ ↓ | 浏览历史 |
| Ctrl+R | 搜索历史 |
| \q | 退出 |
| = 1+2 | 打印表达式结果 |
| help / copyright / license | 查看信息 |

---

## 类型映射

| Lua | Java 方向 |
|-----|-----------|
| integer | Integer / int 双向 |
| number | Double / double 双向 |
| string | String 双向 |
| boolean | Boolean 双向 |
| nil | null / void 返回 |
| userdata | Java 对象 双向 |
| function | LuaFunctionObj Lua → Java |

---

## API 参考

### LuaRuntime（Java 侧入口）

| 方法 | 说明 |
|------|------|
| LuaRuntime() | 创建 Lua 虚拟机 |
| doString(script) | 执行 Lua 代码 |
| doFile(path) | 执行 Lua 文件 |
| callFunction(name, ...) | 调用全局函数，返回第一个值 |
| callFunctionMultiple(name, ...) | 调用全局函数，返回所有值 |
| compile(code) | 编译为 LuaFunctionObj |
| registerModule(obj) | 注册带 @LuaModule 注解的对象 |
| close() | 关闭虚拟机 |

### Lua 侧 java 库

| 函数 | 说明 |
|------|------|
| java.import("类名") | 导入 Java 类 |
| 类:new(...) | 调用构造方法 |
| 对象:方法(...) | 调用实例方法 |
| 类.静态方法(...) | 调用静态方法 |
| java.createProxy({接口...}, 表) | Lua 表实现 Java 接口 |
| java.newArray("类型", 大小) | 创建 Java 数组 |
| java.promise() | 创建异步 Promise |
| java.runAsync(id, class, method, args...) | 异步调用静态方法 |
| java.runAsyncObj(id, obj, method, args...) | 异步调用实例方法 |
| java.checkPromise(id) | 轮询 Promise 结果 |
| java.getObject(id) | 获取异步构造的对象 |

### Lua 侧 clac 库

| 函数 | 说明 |
|------|------|
| clac.array(n) | 创建 ClacArray |
| clac.pi() / clac.e() | 数学常量 |
| clac.sin(x) / clac.cos(x) / ... | 标量数学函数 |
| clac.batch_add(a, b) / batch_sub(...) | 批量四则运算 |
| clac.batch_sin(a) / batch_cos(a) / ... | 批量一元函数 |
| clac.batch_pow(a, b) / batch_atan2(a, b) / ... | 批量二元函数 |
| clac.random() / clac.seed() | 随机数生成 |

### Lua 侧 utils 库

| 函数 | 说明 |
|------|------|
| utils.ns_time() | 高精度时间（秒） |
| utils.monotonic_time() | 单调时间（秒） |
| utils.timestamp() | Unix 时间戳（秒） |
| utils.timestamp_ms() | Unix 时间戳（毫秒） |
| utils.sleep(sec) | 休眠指定秒数 |
| utils.sleep_ms(ms) | 休眠指定毫秒数 |
| utils.sleep_us(us) | 休眠指定微秒数 |
| utils.sleep_ns(ns) | 休眠指定纳秒数 |
| utils.timer() | 创建计时器对象 |

### Lua 侧 gc 库

| 函数 | 说明 |
|------|------|
| gc.hold(obj) | 创建强引用 |
| gc.holdWeak(obj) | 创建弱引用 |
| gc.get(ref) | 获取引用对象 |
| gc.release(ref) | 释放引用 |
| gc.exists(ref) | 检查引用是否存在 |
| gc.count() | 获取引用数量 |
| gc.list() | 列出所有引用 ID |
| gc.clear() | 清除所有引用 |

---

## 构建

要求：JDK 17+、CMake 3.14+、GCC/Clang、pthread。

~~~bash
git clone https://github.com/npp-zep/LuaJavaNE.git
cd LuaJavaNE
make        # 标准构建
# 或
make ninja  # Ninja 快速构建
make test   # 运行测试
~~~

---

## 已知限制

- compile() 方法在 x86_64 Linux 上可能崩溃（ARM64 正常）
- 方法重载匹配按返回类型优先级尝试，复杂重载可能不精确
- 异步方法目前仅支持单个参数（后续扩展）
- Termux/Android 环境下线程数上限约 200（受系统限制）

---

## 贡献

~~~bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
git checkout -b feature/my-feature
# 修改代码
make && make test
git commit -m "Description"
git push origin feature/my-feature
# 在 GitHub 上提 Pull Request
~~~

---

## 许可证

本项目主体采用 MIT License。

### 第三方依赖

- **Lua 5.4.8** — MIT License
- **JLine 3** — BSD-3-Clause License
- **Sleef** — Boost Software License 1.0
- **JUnit 5** — EPL-1.0（仅用于测试，不包含在发布包中）

作者: npp-zep