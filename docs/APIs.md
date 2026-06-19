
# LuaJavaNE API Reference

## 1. Lua 侧 Java 互调

### 1.1 导入 Java 类

```lua
local java = require("java")
local String = java.import("java.lang.String")
local System = java.import("java.lang.System")
```

java.import(className) 返回一个 Java 类 userdata，可以：

· 调用静态方法：System.getProperty("java.version")
· 构造实例：String:new("hello")
· 访问静态字段：Integer.MAX_VALUE

1.2 调用静态方法

```lua
local Integer = java.import("java.lang.Integer")
print(Integer.parseInt("42"))       -- 42
print(Integer.toHexString(255))     -- "ff"
```

注意：参数类型会自动推导（整数→int/long，浮点数→double，字符串→String，布尔→boolean）。

1.3 创建对象与实例方法

```lua
local s = String:new("Hello LuaJavaNE")
print(s:length())                   -- 20
print(s:toUpperCase())              -- "HELLO LUAJAVANE"
```

实例方法使用冒号 :，会将自身作为第一个参数传递。
已知限制：部分无参实例方法（如 accept()、getInputStream()）可能无法匹配，请使用下文兼容层或 Java 辅助类。

1.4 字段访问

```lua
local point = java.import("java.awt.Point"):new(100, 200)
print(point.x)   -- 100.0
point.y = 300
```

1.5 数组操作

```lua
local arr = java.newArray("int", 3)
arr[0] = 100
arr[1] = 200
arr[2] = 300
print(#arr)         -- 3
print(arr[1])       -- 200
```

支持的类型："int", "double", "boolean", "String"。

1.6 动态代理（实现 Java 接口）

```lua
local Runnable = java.import("java.lang.Runnable")
local handler = { run = function(self) print("Hello from Lua!") end }
local proxy = java.createProxy({"java.lang.Runnable"}, handler)
java.import("java.lang.Thread"):new(proxy):start()
```

2. Java 侧 Lua 互调

2.1 基本使用

```java
LuaRuntime L = new LuaRuntime();
L.doString("print('hello')");
L.doString("function add(a,b) return a+b end");
Object result = L.callFunction("add", 3, 5); // 8
L.close();
```

2.2 注解绑定

```java
@LuaModule("math")
class MyMath {
    @LuaFunction public int add(int a, int b) { return a+b; }
    @LuaFunction("multiply") public int mul(int a, int b) { return a*b; }
}
L.registerModule(new MyMath());
// Lua 中可调用 math_add(3,5) 和 math_multiply(6,7)
```

2.3 编译函数

```java
LuaFunctionObj fn = L.compile("return function(x) return x*2 end");
LuaFunctionObj doubler = (LuaFunctionObj) fn.call();
doubler.call(21); // 42
fn.destroy(); doubler.destroy();
```

3. Agent v2 异步调用

3.1 异步静态方法

```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
-- 轮询结果（必须搭配 Thread.sleep 让出 CPU）
local Thread = java.import("java.lang.Thread")
while true do
    local done, result = java.checkPromise(id)
    if done then
        print(result) -- 42
        break
    end
    Thread.sleep(10)
end
```

3.2 异步实例方法

```lua
local s = java.import("java.lang.String"):new("Hello")
local id = java.promise()
java.runAsyncObj(id, s, "length")
repeat
    local done, result = java.checkPromise(id)
    Thread.sleep(10)
until done
print(result) -- 5
```

3.3 异步构造对象

```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello")
-- 获取对象 ID 并转换为 userdata
local oid
repeat
    local done, tmp = java.checkPromise(id)
    oid = tmp
    Thread.sleep(10)
until done
local obj = java.getObject(oid)
print(obj:length()) -- 5
```

3.4 多返回值

```lua
local s = String:new("a,b,c")
local id = java.promise()
java.runAsyncObj(id, s, "split", ",")
local done, a, b, c
repeat
    done, a, b, c = java.checkPromise(id)
    Thread.sleep(10)
until done
print(a, b, c) -- a   b   c
```

3.5 错误处理

```lua
java.runAsync(id, "java.lang.NonExistent", "foo", "")
-- 结果字符串："java.lang.ClassNotFoundException: java.lang.NonExistent"
```

3.6 注意事项

· 必须在轮询循环中调用 Thread.sleep()，让出 CPU 给后台线程。
· 线程池为守护线程，进程退出时自动终止，不保证未完成任务执行完毕。
· 单个异步调用参数类型会自动推导，但复杂参数建议使用兼容层或辅助类。

4. Clac 高性能数学库

4.1 基本数学函数

```lua
local clac = require("clac")
print(clac.pi())             -- 3.1415926535898
print(clac.sin(1.57))        -- 0.999...
print(clac.sqrt(144))        -- 12.0
print(clac.erf(1.0))         -- 0.8427...
```

支持的函数：add, sub, mul, div, abs, floor, ceil, round, trunc, min, max, pow, sqrt, cbrt, hypot, exp, exp2, expm1, log, log10, log2, log1p, sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh, asinh, acosh, atanh, erf, erfc, tgamma, lgamma, copysign, fmod, remainder, nextafter, pi, e, random, seed, deg, rad

4.2 批量数组运算

```lua
local a = clac.array(10000)
local b = clac.array(10000)
-- 填充...
local c = clac.batch_add(a, b)   -- 比 Lua 表循环快 83 倍
local s = clac.batch_sin(a)      -- 逐元素正弦
```

支持 batch_add, batch_sub, batch_mul, batch_div, batch_sin。

5. Java 兼容层

为解决部分 Java 标准库方法在 LuaJavaNE 中调用不便的问题，提供了一系列静态封装方法，位于 com.luajava.compat 包中。使用方法同普通 Java 类导入：

```lua
local File = java.import("com.luajava.compat.FileCompat")
print(File.readFile("test.txt"))
```

5.1 FileCompat

```lua
readFile(path)       -- 返回字符串
writeFile(path, str)
exists(path)         -- 返回 boolean
isDir(path)
listDir(path)        -- 返回字符串数组
delete(path)
fileSize(path)       -- 返回字节数
```

5.2 HttpClientCompat

```lua
get(url)             -- 返回响应体字符串
post(url, body)
responseCode(url)    -- 返回 int
```

5.3 CryptoCompat

```lua
md5(input)           -- 返回 hex 字符串
sha1(input)
sha256(input)
base64Encode(input)
base64Decode(input)
```

5.4 RegexCompat

```lua
matches(pattern, input)         -- 是否完全匹配
find(pattern, input)            -- 返回第一个匹配
findAll(pattern, input)         -- 返回所有匹配的数组
replace(pattern, input, repl)   -- 替换所有匹配
```

5.5 URLCompat

```lua
encode(input)        -- URL 编码
decode(input)        -- URL 解码
```

5.6 DateTimeCompat

```lua
now()                -- ISO 时间戳
nowLocal()           -- 本地时间格式化
timestamp()          -- 毫秒级 Unix 时间戳
formatTime(ts)       -- 毫秒数转 ISO 字符串
```

5.7 UUIDCompat

```lua
randomUUID()         -- 随机 UUID 字符串
fromString(name)     -- 命名 UUID
```

5.8 SystemCompat

```lua
getProperty(key)     -- 系统属性
getenv(key)          -- 环境变量
currentTimeMillis()
nanoTime()
gc()
availableProcessors()
maxMemory()          -- 字节数
```

5.9 其他

· IOCompat：ServerSocket/Socket/Stream 的静态包装
· CollectionCompat：ArrayList/HashMap 操作
· StringCompat：getBytes, substring
· ThreadCompat：sleep, currentThreadName
· ProcessCompat：exec, execExitCode

6. 全局存储

```lua
java.store("key", "value")      -- 存储到 C 端全局链表
local val = java.fetch("key")   -- 读取
java.deleteStore("key")         -- 删除
```

适合高频共享数据，避免反复跨 JNI。

7. 类型映射总结

Lua 类型 Java 类型 方向
integer Integer / int 双向
number Double / double 双向
string String 双向
boolean Boolean 双向
nil null / void 返回值
userdata Java 对象 双向
function LuaFunctionObj Lua→Java

---

更多示例请参见项目 examples/ 目录。

