LuaJavaNE API Reference

完整的双向互操作 API 文档。涵盖 Lua 调用 Java、Java 调用 Lua、异步任务、高性能数学库等核心功能。

---

1. Lua 侧 Java 互调

1.1 加载模块

```lua
local java = require("java")
```

返回值是一个包含所有 Java 互操作函数的表。

---

1.2 java.import(className)

导入一个 Java 类，返回代表该类的 userdata。

参数 类型 说明
className string 全限定类名，如 "java.lang.String"

返回值：Java 类 userdata（可用作构造器、访问静态成员）。

示例：

```lua
local String = java.import("java.lang.String")
local System = java.import("java.lang.System")
```

---

1.3 静态方法调用

通过类 userdata 直接调用静态方法（使用点号 .）：

```lua
local Integer = java.import("java.lang.Integer")
local result = Integer.parseInt("42")          -- 42
local hex = Integer.toHexString(255)           -- "ff"
local max = Integer.MAX_VALUE                  -- 2147483647
```

参数类型自动转换（见第 7 节类型映射）。

---

1.4 构造对象与实例方法

使用 :new(...) 构造对象，然后使用冒号 : 调用实例方法（自动传递 self）：

```lua
local s = String:new("Hello")
print(s:length())          -- 5
print(s:toUpperCase())     -- "HELLO"
print(s:substring(0, 2))   -- "He"
```

注意：某些方法签名可能无法自动匹配（尤其无参方法），建议使用 Java 辅助类或通过其他方式间接调用。

---

1.5 字段访问

读取或写入 Java 对象的 public 字段：

```lua
local Point = java.import("java.awt.Point")
local p = Point:new(10, 20)
print(p.x)    -- 10.0
p.y = 100
```

---

1.6 数组操作

创建 Java 数组：

```lua
local arr = java.newArray("int", 5)
arr[0] = 100
arr[1] = 200
print(#arr)         -- 5（获取长度）
print(arr[1])       -- 200
```

支持的元素类型："int", "double", "boolean", "String"。

---

1.7 动态代理（实现 Java 接口）

java.createProxy(interfaces, handler) 创建代理对象。

参数 类型 说明
interfaces table 接口名字符串数组，如 {"java.lang.Runnable"}
handler table 包含方法实现的 Lua 表，键为方法名，值为函数

```lua
local Runnable = java.import("java.lang.Runnable")
local handler = {
    run = function(self)
        print("Running in proxy!")
    end
}
local proxy = java.createProxy({"java.lang.Runnable"}, handler)
local thread = java.import("java.lang.Thread"):new(proxy)
thread:start()
```

方法可以返回值，会被自动转换为 Java 对象。

---

1.8 其他工具

java.toString(obj)

将 Java 对象转为 Lua 字符串（调用其 toString() 方法）。

```lua
local s = java.toString(javaObject)
```

java.store(key, value) / java.fetch(key) / java.deleteStore(key)

跨语言全局键值存储（C 端哈希表），适合高频数据共享（详见第 5 节）。

---

2. Java 侧 Lua 互调

Java 侧使用 LuaRuntime 类（位于 com.luajava 包）执行 Lua 代码。

2.1 基本使用

```java
LuaRuntime L = new LuaRuntime();
L.doString("print('hello')");
L.doString("function add(a,b) return a+b end");
Object result = L.callFunction("add", 3, 5); // 返回 Integer 8
L.close(); // 释放资源
```

2.2 设置/获取全局变量

```java
L.setGlobal("name", "Lua");
String name = L.getGlobal("name"); // "Lua"
```

2.3 调用 Lua 函数（多返回值）

```java
Object[] results = L.callFunctionMultiple("add", 3, 5);
// results[0] = 8
```

2.4 编译可复用函数

```java
LuaFunctionObj fn = L.compile("return function(x) return x*2 end");
LuaFunctionObj doubler = (LuaFunctionObj) fn.call(); // 调用返回闭包
int doubled = (int) doubler.call(21); // 42
fn.destroy();
doubler.destroy();
```

2.5 注解绑定 Java 方法

使用 @LuaModule 和 @LuaFunction 注解：

```java
@LuaModule("math")
public class MyMath {
    @LuaFunction
    public int add(int a, int b) { return a + b; }
    
    @LuaFunction("mul")
    public int multiply(int a, int b) { return a * b; }
}
L.registerModule(new MyMath());
// Lua 中调用 math_add(3,5) 和 math_mul(6,7)
```

2.6 执行 Lua 文件

```java
L.doFile("script.lua");
```

2.7 资源管理

LuaRuntime 实现了 AutoCloseable，可用 try-with-resources 自动释放：

```java
try (LuaRuntime L = new LuaRuntime()) {
    L.doString("print('auto closed')");
}
```

---

3. Agent v2 异步任务

所有异步 API 位于 java 模块中。

3.1 java.promise()

创建一个新的 Promise ID。

返回值：整数 ID（用于后续任务提交和轮询）。

---

3.2 java.runAsync(id, className, methodName, ...args)

异步调用静态方法。

参数 类型 说明
id number Promise ID
className string 全限定类名
methodName string 方法名（构造方法用 "new"）
...args any 零个或多个参数

示例：

```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
```

---

3.3 java.runAsyncObj(id, obj, methodName, ...args)

异步调用实例方法。

参数 类型 说明
id number Promise ID
obj userdata Java 对象
methodName string 方法名
...args any 参数

```lua
local s = String:new("Hello")
local id = java.promise()
java.runAsyncObj(id, s, "length")
```

---

3.4 java.checkPromise(id)

轮询任务结果。

返回值：

· 如果未完成：返回 (false)（只有一个值）。
· 如果已完成：返回 (true, result1, result2, ...)，结果数量取决于方法返回值（多返回值会被展开）。

```lua
local done, result
repeat
    done, result = java.checkPromise(id)
    if not done then
        java.import("java.lang.Thread"):sleep(10)
    end
until done
```

---

3.5 java.getObject(id)

获取由异步构造任务返回的对象（对象 ID 从 checkPromise 获得）。

```lua
local done, oid = java.checkPromise(id)
if done and oid then
    local obj = java.getObject(oid)
    print(obj:length())
end
```

---

3.6 任务统计与监控（通过 LuaAgent 类）

导入 com.luajava.LuaAgent：

```lua
local LuaAgent = java.import("com.luajava.LuaAgent")
```

方法 返回值 说明
LuaAgent:getTaskStats() string 提交/完成/失败/超时/取消/运行数
LuaAgent:getExecutorStats() string 池大小/活跃/队列/已完成任务
LuaAgent:getRunningTaskCount() number 当前运行的任务数
LuaAgent:resetStats() void 重置所有计数器
LuaAgent:cancelTask(pid, mayInterrupt) boolean 取消指定任务

```lua
print(LuaAgent:getTaskStats())
LuaAgent:cancelTask(pid, true)
```

---

4. Clac 高性能数学库

加载：local clac = require("clac")

4.1 标量函数

所有函数直接使用，支持常用数学运算。

基本运算：add, sub, mul, div

取整与绝对值：abs, floor, ceil, round, trunc, rint, nearbyint

幂指对：pow, sqrt, cbrt, hypot, exp, exp2, expm1, log, log10, log2, log1p

三角函数：sin, cos, tan, asin, acos, atan, atan2

双曲函数：sinh, cosh, tanh, asinh, acosh, atanh

特殊函数：erf, erfc, tgamma, lgamma

浮点操作：copysign, fmod, remainder, nextafter, fma, fdim

分类：isfinite, isinf, isnan, isnormal, signbit

极值：min, max, fmax, fmin

角度转换：deg（弧度→度）, rad（度→弧度）

常数：pi(), e()

随机数：random()（[0,1) 双精度）, random(max)（1~max 整数）, random(min, max)（整数区间）, seed()（用时间初始化）

```lua
local clac = require("clac")
print(clac.sin(clac.pi()/2))  -- 1.0
print(clac.pow(2, 10))        -- 1024.0
print(clac.erf(1.0))          -- 0.8427
```

---

4.2 批量数组运算

clac.array(size)

创建双精度数组（userdata），支持索引和长度。

```lua
local a = clac.array(100)
a[1] = 3.14
print(#a)        -- 100
print(a[1])      -- 3.14
```

批量操作函数

所有 batch_* 函数支持两个数组逐元素运算，返回新数组。一元函数接受一个数组，二元接受两个数组（长度必须一致）。

一元批量：batch_abs, batch_floor, batch_ceil, batch_round, batch_trunc, batch_rint, batch_nearbyint, batch_sqrt, batch_cbrt, batch_exp, batch_exp2, batch_expm1, batch_log, batch_log10, batch_log2, batch_log1p, batch_sin, batch_cos, batch_tan, batch_asin, batch_acos, batch_atan, batch_sinh, batch_cosh, batch_tanh, batch_asinh, batch_acosh, batch_atanh, batch_erf, batch_erfc, batch_tgamma, batch_lgamma, batch_logb, batch_deg, batch_rad

二元批量：batch_add, batch_sub, batch_mul, batch_div, batch_pow, batch_atan2, batch_hypot, batch_copysign, batch_fmod, batch_remainder, batch_nextafter, batch_fmax, batch_fmin, batch_fdim

```lua
local a = clac.array(10000)
local b = clac.array(10000)
for i = 1, 10000 do a[i] = math.random(); b[i] = math.random() end
local c = clac.batch_add(a, b)
local s = clac.batch_sin(a)
```

性能比纯 Lua 循环快数十倍（SIMD 加速）。

---

5. 全局存储

C 端全局哈希表（键值对），适合高频共享数据，避免 JNI 开销。

```lua
java.store("key", "value")      -- 存储
local val = java.fetch("key")   -- 读取
java.deleteStore("key")         -- 删除
```

性能对比见 bench_store_calc.lua。

---

6. AsyncRunner 工具类

com.luajava.AsyncRunner 提供同步调用和类型判断辅助方法（在 Lua 中可通过 java.import 获得）。

AsyncRunner.isInstance(obj, className)

判断 Java 对象是否属于某个类或实现了某个接口。

```lua
local AsyncRunner = java.import("com.luajava.AsyncRunner")
local isRunnable = AsyncRunner.isInstance(proxy, "java.lang.Runnable")
```

---

7. 类型映射总结

Lua 类型 Java 参数类型（调用 Java 方法时） Java 返回值转为 Lua 类型
nil null 引用或 void nil
boolean boolean / Boolean boolean
number（整数） int / long / Integer / Long number（整数）
number（浮点） double / float / Double / Float number（浮点）
string String string
userdata（Java） 原 Java 对象类型 同类型 userdata
function（Lua） 可转为 LuaFunctionObj（传递给 Java） 不支持自动转换

数组返回值：Java 数组（如 String[]）在 Lua 中表现为 userdata，支持 # 获取长度，支持索引访问（从 0 开始）。

---

8. 快速示例汇总

基本交互

```lua
local java = require("java")
local System = java.import("java.lang.System")
local String = java.import("java.lang.String")

print(System.currentTimeMillis())
local s = String:new("hello")
print(s:toUpperCase())
```

异步任务

```lua
local id = java.promise()
java.runAsync(id, "java.lang.Math", "pow", 2, 10)
local done, result
repeat
    done, result = java.checkPromise(id)
    Thread.sleep(10)
until done
print(result)  -- 1024.0
```

高性能计算

```lua
local clac = require("clac")
local a = clac.array(1000)
for i = 1, 1000 do a[i] = i end
local b = clac.batch_sin(a)
print(b[10])
```

---

完整示例请参考项目 examples/ 目录。