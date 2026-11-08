# LuaJavaNE

![Lua](https://img.shields.io/badge/Lua-5.4.8-blue) ![Java](https://img.shields.io/badge/Java-17%2B-orange) ![License](https://img.shields.io/badge/license-MIT-green)

**Lua 5.4.8 + Java 双向互调引擎**。在 Lua 里直接调用任何 Java 类库，在 Java 里执行 Lua 脚本。  
支持异步多线程（轮询/回调双模式）、动态代理、批量高性能数学运算，架构清晰，升级 Lua 仅需替换目录。

> 完整 API 文档见 `docs/` 目录：[APIs.md](docs/APIs.md) · [AgentV2.md](docs/AgentV2.md) · [Java4Lua.md](docs/Java4Lua.md) · [Lua4Java.md](docs/Lua4Java.md)

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

### 运行期加载 JAR

~~~lua
java.load("libs/mylib.jar")          -- 返回新增条目数；重复加载返回 0
local Greeter = java.import("demo.Greeter")
print(Greeter.hi("lua"))
~~~

也接受散装 class 目录（`java.load("build/classes")`）与多个路径（`java.load("a.jar", "b.jar")`）。加载失败会抛出 Lua 错误，信息包含具体原因。

若 JAR 依赖自带的本地库（`.so` / `.dylib` / `.dll`），把选项表放在参数末尾：

~~~lua
local added, warnings = java.load("libs/native-lib.jar", {natives=true})
for _, w in ipairs(warnings) do print("[warn] " .. w) end
~~~

`natives=true` 会按当前平台（如 `linux-x86_64`）筛选 JAR 内本地库，解压到临时目录并注册给加载器的 `findLibrary`，JAR 代码里的 `System.loadLibrary("foo")` 才能绑定成功。同名类覆盖、跨 JAR 重复、平台不匹配等告警会打印到 stderr，同时作为第二个返回值返回。

> JAR 通过独立的 `URLClassLoader`（父加载器为应用类加载器）加载，因此 classpath 中已有的同名类优先（会在加载时给出 `shadowed` 告警），且 JVM 无法卸载已加载的类；同一路径重复 `java.load` 不会生效。仅 `java.load` 能访问的类，其 `getResource`/`ServiceLoader`（TCCL）仍然看不到 JAR 内资源。

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

### 异步参数类型

- 标量：`string`、`number`、`boolean`、`char` 等按类型提示自动匹配。
- Java 对象/数组：直接传 userdata，底层登记到参数对象池并以对象 id 传递，任务结束后自动释放；会按目标形参类型做赋值检查。
- `table` / `function` 不能作为异步参数，会被字符串化或转成空字符串。

### 多返回值

~~~lua
local s = String:new("a,b,c")
local id = java.promise()
java.runAsyncObj(id, s, "split", ",")
repeat local done, a, b, c = java.checkPromise(id) until done
print(a, b, c)  -- a   b   c
~~~

### 错误处理

异步任务中抛出的异常会被捕获并作为字符串返回，消息格式为
`类名.方法 -> 异常类型: 消息`（包含根因信息）：

~~~lua
java.runAsync(id, "java.lang.NonExistent", "foo", "")
-- checkPromise 返回: "java.lang.NonExistent.foo -> ClassNotFoundException: java.lang.NonExistent"
~~~

Lua 同步调用 Java 方法抛出异常时，会得到包含类型与消息的 Lua 错误，可用 `pcall` 捕获：

~~~lua
local ok, err = pcall(function() return obj:riskyMethod() end)
-- err 形如: Java exception in riskyMethod: java.lang.IllegalStateException: detail
~~~

同步调用（Java 侧）的错误则抛出结构化异常，全部继承 `RuntimeException`，对既有
`catch (RuntimeException)` 完全向后兼容：

- `LuaRuntimeError` —— Lua 执行期错误
- `LuaSyntaxError` —— Lua 编译期语法错误（`LuaRuntimeError` 的子类）
- `JavaInvocationError` —— Lua 调用已注册 Java 方法失败
- `TypeConversionError` —— Lua ⇄ Java 参数类型转换失败

### 回调式消费（java.onComplete）

除了轮询 `checkPromise`，还可以注册完成回调，任务完成时由后台线程自动调用：

~~~lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
java.onComplete(id, function(err, result)
    if err then print("失败:", err) else print("结果:", result) end  -- 42
end)
~~~

- 回调签名：`callback(err, result...)`，`err == nil` 表示成功。
- 回调在后台工作线程执行，应快速返回；耗时的重活请再次 `runAsync` 提交。
- 更多异步 API 细节见 `docs/AgentV2.md`。

### 释放锁等待（java.yield）

主线程轮询等待期间，用 `java.yield(ms)` 短暂释放 Lua 锁（默认 10ms），让后台回调/代理线程有机会执行 Lua 代码，避免持锁等待导致死锁（`Thread.sleep` 不释放锁）：

~~~lua
local done = false
java.onComplete(id, function(err, result) done = true end)
while not done do java.yield(10) end
~~~

### 协程等待（java.await）

`java.await(id)` 在协程内挂起，Java 任务完成后由工作线程恢复协程，适合顺序化写法：

~~~lua
local co = coroutine.create(function()
    local id = java.promise()
    java.runAsync(id, "java.lang.Thread", "sleep", 200)
    java.await(id)                       -- 挂起，任务完成后恢复
    print("done")
end)
coroutine.resume(co)
while coroutine.status(co) ~= "dead" do java.yield(5) end
~~~

- 任务已完成时立即返回，结果语义与 `checkPromise` 一致；
- 任务失败时返回 `E:` 后的错误文本（不抛出异常）；
- 在非协程环境等待未完成的任务会报 `attempt to yield from outside a coroutine`；
- 主线程仍需用 `java.yield` 让出 Lua 锁，否则工作线程无法恢复协程。

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

批量运算在数组长度 ≥ 4096 时自动启用 OpenMP 多线程（`OMP_NUM_THREADS` 可调）；数学函数使用 Sleef AVX2 向量内核（`batch_sqrt`/`batch_abs` 直接使用硬件指令），尾部元素与不支持 AVX2 的环境自动回退标量，结果一致。

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
| ↑ ↓ | 在物理行间移动；到达首/末行后浏览历史 |
| Ctrl+R | 搜索历史 |
| 空行回车 | 提交多行语句块（单行完整语句回车即执行） |
| \q | 退出 |
| = 1+2 | 打印表达式结果 |
| help / copyright / license | 查看信息 |

多行输入时续行提示符为 `>> `，并按 Lua 关键字自动缩进；未闭合的字符串/括号会保持续行，Ctrl+C 放弃当前未提交的块。整块作为一条历史记录保存。

---

## 类型映射

| Lua | Java 方向 |
|-----|-----------|
| integer | Integer / int 双向 |
| number | Double / double 双向 |
| string | String 双向 |
| boolean | Boolean 双向 |
| string（单个码点）或 integer | char / Character 双向 |
| nil | null / void 返回 |
| userdata | Java 对象 双向 |
| function | LuaFunctionObj Lua → Java |
| table | LuaTable（Lua → Java 惰性引用） |
| Map / List / Collection（Java 返回） | 惰性容器 userdata（读取/遍历/写回/追加，可存 Lua 函数读回可调用） |

> 字符串按标准 UTF-8 双向转换：emoji 等增补平面字符（4 字节 UTF-8）可正确往返；非法 UTF-8 或孤立 UTF-16 代理项会替换为 `U+FFFD`。Java `char` 只能表示 BMP 字符。

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
| java.load("lib.jar", ...) | 运行期附加 JAR/ZIP 或 class 目录，之后可 import 其中的类；`{natives=true}` 同时绑定 JAR 内本地库（返回新增条目数 + 告警列表） |
| 类:new(...) | 调用构造方法 |
| 对象:方法(...) / 对象(...) | 调用实例方法；对象可直接调用（`__call`，调用其 `call` 方法） |
| 类.静态方法(...) / 类:静态方法(...) | 调用静态方法（冒号形式自动剥离类 self 参数） |
| java.createProxy({接口...}, 表) | Lua 表实现 Java 接口 |
| java.newArray("类型", 大小) | 创建 Java 数组 |
| java.import("com.luajava.LuaTable") | Java 侧以 LuaTable 接收 Lua 表（活引用，见 docs/Java4Lua.md 第 8 节） |
| 容器（Java 返回的 Map/List） | JavaTable 惰性容器：读取/遍历/写回/`#`，`l[#l+1]=v` 追加，可存 Lua 函数读回可调用 |
| java.promise() | 创建异步 Promise |
| java.runAsync(id, class, method, args...) | 异步调用静态方法 |
| java.runAsyncObj(id, obj, method, args...) | 异步调用实例方法 |
| java.checkPromise(id) | 轮询 Promise 结果 |
| java.onComplete(id, callback) | 注册完成回调（事件驱动） |
| java.complete(id, value) | 手动完成一个 Promise |
| java.await(id) | 协程内挂起等待 Promise 完成（任务完成后由工作线程恢复协程） |
| java.yield(ms) | 释放 Lua 锁等待，让后台回调执行 |
| java.getObject(id) | 获取异步构造的对象 |
| java.toString(obj) | 获取对象的字符串表示 |
| java.store(key, value) | 跨 Lua 状态存储值 |
| java.fetch(key) | 读取跨状态存储值 |
| java.listall() | 列出所有存储键值对（返回 key -> value 表） |
| java.deleteStore(key) | 删除跨状态存储键 |
| java.cleanup() | 清空跨状态存储的全部键值 |

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

可选依赖（缺失时自动回退，不影响构建）：

- **OpenMP** — `clac` 批量运算多线程（GCC 自带 `libgomp`）
- **Sleef** — `clac` 批量数学的 AVX2 向量内核（需同时具备 `sleef.h` 与 `libsleef`，`-DLUAJAVA_ENABLE_SLEEF=OFF` 可关闭）

~~~bash
git clone https://github.com/npp-zep/LuaJavaNE.git
cd LuaJavaNE
make        # 标准构建
# 或
make ninja  # Ninja 快速构建
make test   # 运行测试
~~~

---

## 已知问题与限制

### 参数匹配与重载

- 6 个参数以内优先走快速签名匹配；**7 个及以上自动回退到反射调用**，结果正确但性能低于快路径。
- 反射回退按“精确度打分”选择重载；泛型擦除、可变参数或高度动态的同名方法仍可能选不到预期版本，失败时报 `no matching method: ... (原因)`。
- 调用 Java 方法抛出的异常会以 Lua 错误形式抛出，信息包含 `java.lang.异常类: message`；异步任务的错误格式为 `类名.方法 -> 异常类型: 消息`。

### 多线程与 Lua 状态

- 所有 `LuaRuntime` 实例共享一把递归 `lua_mutex`：一个状态长时间执行 Lua 代码会阻塞其他状态的调用和回调。
- 异步回调、代理回调与 `java.await` 恢复都依赖主线程用 `java.yield(ms)` 让出锁；持锁死等会形成死锁。
- 同一个 `LuaRuntime` 不应被多个线程同时直接操作（Lua 状态本身非线程安全）。
- `java.store` 是**进程级全局表**（跨状态共享是设计特性），不随单个 `LuaRuntime` 关闭而清空；可用 `java.cleanup()` 整体清空。

### 异步 API

- Lua 侧未暴露取消和超时参数；底层默认超时 300 秒。
- 对象参数通过参数对象池传递，任务结束由 `AsyncRunner` 释放；若线程池直接拒绝任务（`AbortPolicy`），引用可能残留到进程结束（极端边界）。
- `table` / `function` 不能作为异步参数；会被字符串化或转成空字符串。
- `java.await` 失败时返回 `E:` 后的错误文本而不是抛出异常；非协程环境等待未完成任务会报 `attempt to yield from outside a coroutine`。
- `onComplete` 回调运行在工作线程，必须快速返回；耗时操作应再次 `runAsync`。

### Unicode 与字符串

- Java `String` 中的 NUL 可保留在 Lua 字符串中，但部分按 C 字符串处理的 API（如 `luaL_checkstring` 路径）可能截断。
- 非法 UTF-8 字节序列、孤立代理项统一替换为 `U+FFFD`，不做报错。
- Java `char` / `Character` 只能表示 BMP 码点；Lua 侧传入增补平面字符时只能取高代理项。

### 运行期加载 JAR

- 类无法卸载：JAR 加载后常驻，同路径重复 `java.load` 不会重新读取文件。
- 父加载器优先：classpath 中已有同名类会胜出，加载时会给出 `shadowed` 告警（最多 10 条，之后折叠为 `further warnings suppressed`）。
- 同名类跨 JAR 重复时，只有先加载的那份生效，加载第二个 JAR 时会给出 `exists in both ...` 告警。
- 只影响类加载：`getResource`、`ServiceLoader`（TCCL）看不到 JAR 内资源。
- 本地库需显式 `{natives=true}`：只提取当前平台匹配的条目；JAR 代码必须自己调用 `System.loadLibrary` / `System.load` 才会绑定，声明了 native 方法却从不加载的库无法自动生效。
- 本地库解压到 `java.io.tmpdir/luajavane-natives/`，磁盘不可写或库格式不匹配会抛出 `java.load failed: java.io.IOException: ...`。
- `URLClassLoader` 持有 jar 文件句柄，Windows 下运行期无法删除或覆盖该 jar。
- `java.load` 会执行任意本地代码（含本地库），只应加载可信 JAR。

### 平台与构建

- Termux/Android 环境下线程数上限约 200（受系统限制）。
- OpenMP、Sleef 为可选依赖，缺失时自动回退到串行/标量实现，性能下降但功能不变。
- 项目绑定 Lua 5.4.x；升级 Lua 需整体替换 `lua/lua-5.x` 目录（Lua 源码零改动）。

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