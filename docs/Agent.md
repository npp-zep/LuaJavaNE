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
· 无界任务队列，提交任务不会阻塞。

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
...args any 方法参数（零个或多个，自动类型转换）

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

· 第一个值：boolean，true 表示任务已完成。
· 后续值：任务返回的结果（数量取决于方法返回值）。
· 若任务尚未完成，返回 (false)（仅一个值）。
· 若任务抛出异常，结果将是一个错误字符串（以 E: 开头，或直接为异常消息）。

注意：调用 checkPromise 不会阻塞，必须循环直到返回 true。

---

java.getObject(id)

获取由异步构造任务返回的对象。

参数 类型 说明
id number 对象 ID（从 checkPromise 获取）

返回值：对应的 Java 对象（Lua userdata）。

---

java.import(className)

同步导入 Java 类，返回代表类的 userdata（可用于构造对象或访问静态成员）。

参数 类型 说明
className string 完整类名

示例：

```lua
local String = java.import("java.lang.String")
local s = String:new("hello")
```

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

4. 多返回值（例如 String.split 返回数组）

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

注意：split 返回一个 Java 字符串数组，在传递到 Lua 时会被展开为多个返回值（每个元素对应一个 Lua 值）。请确保变量数量与方法返回值个数匹配，多余的会被忽略。

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

7. 轮询封装（带延迟）

```lua
local function await(id, timeout)
    local elapsed = 0
    local step = 10
    local Thread = java.import("java.lang.Thread")
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        if timeout and elapsed >= timeout then
            return nil, "timeout"
        end
        Thread.sleep(step)
        elapsed = elapsed + step
    end
end

-- 使用
local id = java.promise()
java.runAsync(id, "java.lang.System", "currentTimeMillis")
local time = await(id, 2000)
print(time)
```

---

类型映射

Lua 类型 Java 参数类型（优先级）
number（整数） int / Integer / long / Long / double / Double
number（浮点） double / Double / float / Float
string String
boolean boolean / Boolean
nil null 或空参数（方法无参）
userdata（Java 对象） 原 Java 对象（相同类型）
table（Lua 表） 不直接支持，需手动转换为 Java 对象

方法重载匹配按返回类型优先级尝试，复杂重载可能不精确，建议使用明确类型或使用 Java 强类型封装。

---

性能统计与监控

Agent v2 提供了一些内置统计接口，可通过 java.import("com.luajava.LuaAgent") 访问：

方法 说明
LuaAgent.getTaskStats() 返回任务统计信息（提交、完成、失败、超时、取消、运行数）
LuaAgent.getExecutorStats() 返回线程池状态（池大小、活跃数、队列长度、已完成任务数）
LuaAgent.getRunningTaskCount() 返回当前正在运行的任务数量
LuaAgent.resetStats() 重置所有计数器

示例：

```lua
local LuaAgent = java.import("com.luajava.LuaAgent")
print(LuaAgent:getTaskStats())
-- 输出：Task stats - Submitted: 100, Completed: 95, Failed: 3, Timeout: 1, Cancelled: 1, Running: 2
```

---

任务取消与超时

虽然 Lua 层没有直接暴露取消接口，但可通过 Java API 实现：

```lua
local LuaAgent = java.import("com.luajava.LuaAgent")
-- 取消任务（需知道任务 PID，即 Promise ID）
local cancelled = LuaAgent:cancelTask(pid, true)  -- 第二个参数表示是否中断正在运行的任务
```

建议在提交任务时记录 PID，并在需要时调用。此功能需谨慎使用，避免资源泄漏。

---

注意事项

1. 轮询间隔：避免频繁轮询，建议使用 Thread.sleep(10) 或更高延迟，以节省 CPU。
2. 资源释放：java.getObject 获取的对象会在 Lua GC 时自动释放，无需手动管理。
3. 线程安全：所有 API 都是线程安全的，可在多个 Lua 协程中并发调用。
4. 异常：任务内部抛出的异常会被捕获并转为字符串返回，不会导致 JVM 崩溃。
5. 方法重载：如遇二义性，可尝试使用具体参数类型（例如 java.lang.Integer 与 int）。
6. 内存泄漏：如果频繁创建大量临时对象，注意监控 Java 堆内存；Agent 使用 WeakReference 存储对象，但仍建议及时释放不再使用的引用。
7. JVM 退出：线程池使用 daemon 线程，JVM 退出时会自动终止所有任务，无需手动关闭。

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

内部实现概述

· Java 侧使用 LuaAgent 类（位于 com.luajava 包），负责线程池管理、任务调度、对象注册和统计。
· 任务通过 JNI 提交，执行时由 AsyncRunner 使用 Java 反射调用目标方法。
· 结果通过 LuaAgent.complete JNI 回调返回到 Lua 的 Promise 注册表。
· 所有 Java 对象通过 WeakReference 存储，防止内存泄漏。
· Lua 侧 java 库通过 C 扩展 lualibjava.c 和 lualib_async.c 实现与 JNI 的交互。

---

更多资源

· 项目 GitHub：https://github.com/npp-zep/LuaJavaNE
· 示例脚本：test_luaagent.lua 和 test_proxy.lua 提供了完整的功能演示。
· 如有问题，请提交 Issue 或参与讨论。

---

Happy scripting with LuaJavaNE!