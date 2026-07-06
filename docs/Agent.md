Agent v2 异步编程指南

LuaJavaNE 的 Agent v2 提供了一种简单、高性能的异步任务执行方式。所有任务在后台线程池中运行，主线程通过 Promise 机制轮询结果，不会阻塞 Lua 执行流。

---

核心概念

Promise

· 每个异步任务都有一个唯一整数 ID（Promise ID）。
· 通过 java.promise() 创建，用于标识任务。
· 通过 java.checkPromise(id) 轮询结果，返回 (done, result1, result2, ...)。

任务提交

· 静态方法：java.runAsync(id, className, methodName, ...args)
· 实例方法：java.runAsyncObj(id, obj, methodName, ...args)
· 构造对象：使用 runAsync 并传入 "new" 作为方法名。

线程池

· 后台线程池在首次调用时自动初始化。
· 线程为 daemon 线程，不会阻止 JVM 退出。
· 线程数根据 CPU 核心数自动调整（2 * cores，最少 4 个）。

---

API 参考

java.promise()

创建新的 Promise ID。

```lua
local id = java.promise()
```

返回值：整数 ID。

---

java.runAsync(id, className, methodName, ...args)

异步调用静态方法。

参数 类型 说明
id number Promise ID
className string 完整类名（如 "java.lang.Integer"）
methodName string 方法名（如 "parseInt"）
...args any 方法参数（零个或多个，自动转换类型）

示例：

```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
```

---

java.runAsyncObj(id, obj, methodName, ...args)

异步调用实例方法。

参数 类型 说明
id number Promise ID
obj userdata Java 对象（由 java.import 或 java.getObject 获取）
methodName string 方法名
...args any 方法参数

示例：

```lua
local s = java.import("java.lang.String"):new("Hello World")
local id = java.promise()
java.runAsyncObj(id, s, "length")
```

---

java.checkPromise(id)

轮询任务是否完成。

参数 类型 说明
id number Promise ID

返回值：

· 第一个值：boolean，true 表示任务已完成
· 后续值：任务返回的结果（数量取决于方法返回值）

重要：

· 如果任务尚未完成，返回 (false)。
· 如果任务完成，返回 (true, result1, result2, ...)。
· 如果任务抛出异常，结果会是错误字符串（以 E: 开头或直接为异常消息）。

---

java.getObject(id)

获取由异步构造任务返回的对象。

参数 类型 说明
id number 对象 ID（从 java.checkPromise 获取）

返回值：对应的 Java 对象（Lua userdata）。

---

使用示例

1. 静态方法异步调用

```lua
local java = require "java"

local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")

local done, result
repeat
    done, result = java.checkPromise(id)
until done

print(result)  -- 42
```

---

2. 实例方法异步调用

```lua
local String = java.import("java.lang.String")
local s = String:new("Hello World")

local id = java.promise()
java.runAsyncObj(id, s, "length")

local done, result
repeat
    done, result = java.checkPromise(id)
until done

print(result)  -- 11
```

---

3. 异步构造对象

```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello")

local done, oid
repeat
    done, oid = java.checkPromise(id)
until done

local obj = java.getObject(oid)
print(obj:length())  -- 5
```

---

4. 多返回值（例如 String.split）

```lua
local s = java.import("java.lang.String"):new("a,b,c")

local id = java.promise()
java.runAsyncObj(id, s, "split", ",")

local done, a, b, c
repeat
    done, a, b, c = java.checkPromise(id)
until done

print(a, b, c)  -- a  b  c
```

注意：参数数量需与方法返回值匹配，多余参数为 nil。

---

5. 错误处理

```lua
local id = java.promise()
java.runAsync(id, "java.lang.NonExistent", "foo", "")

local done, result
repeat
    done, result = java.checkPromise(id)
until done

print(result)  
-- 输出: "java.lang.ClassNotFoundException: java.lang.NonExistent"
```

---

6. 并发多个任务

```lua
local count = 10
local ids = {}

for i = 1, count do
    local id = java.promise()
    java.runAsync(id, "java.lang.Thread", "sleep", "50")
    table.insert(ids, id)
end

for _, id in ipairs(ids) do
    repeat
        local done = java.checkPromise(id)
    until done
end
```

10 个 Thread.sleep(50ms) 并发总耗时约 60ms（接近理论最小值 50ms）。

---

类型映射

Lua 类型 Java 参数类型
number (整数) int / Integer / long / Long / double / Double
number (浮点) double / Double / float / Float
string String
boolean boolean / Boolean
nil null 或空参数
userdata Java 对象（原样传递）

方法重载匹配按返回类型优先级尝试，复杂重载可能不精确，建议使用明确类型。

---

性能特性

· 线程池：自动管理，daemon 线程，进程退出时自动关闭。
· 并发度：2 * CPU核心数（最少 4 个线程）。
· 吞吐：50 个并发 Thread.sleep(10ms) 总耗时约 0.05 秒。
· 队列：无界队列，任务提交不会阻塞。

---

注意事项

1. 轮询间隔：避免频繁轮询，建议使用 Thread.sleep(10) 或更高延迟。
2. 资源释放：java.getObject 获取的对象会在 Lua GC 时自动释放，无需手动管理。
3. 线程安全：所有 API 都是线程安全的，可在多个 Lua 协程中并发调用。
4. 异常：任务内部抛出的异常会被捕获并转为字符串返回，不会导致 JVM 崩溃。
5. 方法重载：如遇二义性，可尝试使用具体参数类型（例如 java.lang.Integer 与 int）。

---

与旧版对比

特性 旧版 (Agent v1) Agent v2
异步 API java.async() / java.await() java.runAsync + java.checkPromise
回调方式 阻塞等待 轮询（非阻塞）
多返回值 不支持 支持
错误处理 抛出异常 返回错误字符串
对象构造 不支持 支持
并发性能 单线程 多线程线程池

---

完整示例：轮询封装

```lua
local function await(id)
    local done, result
    repeat
        done, result = java.checkPromise(id)
        if not done then
            java.import("java.lang.Thread"):sleep(10)
        end
    until done
    return result
end

-- 使用
local id = java.promise()
java.runAsync(id, "java.lang.System", "currentTimeMillis")
local time = await(id)
print(time)
```

---

内部实现概述

· Java 侧使用 LuaAgent 类（位于 com.luajava 包）。
· 线程池、对象注册、统计信息由 LuaAgent 管理。
· 任务通过 JNI 执行，结果通过 complete 回调返回 Lua。
· 所有 Java 对象通过 WeakReference 存储，防止内存泄漏。

---

