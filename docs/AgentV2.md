# Agent V2 API 文档

## 概述

Agent V2 是 LuaJavaNE 提供的**异步任务执行框架**，允许你在 Lua 脚本中**非阻塞地执行 Java 方法**（包括静态方法、实例方法、构造器），并在后台线程池中处理计算密集型或 I/O 阻塞操作。任务结果通过 **Promise 机制** 异步返回，无需阻塞 Lua 主协程。

主要特性：
- **非阻塞执行**：所有任务在独立线程池中运行，不阻塞 Lua 主线程。
- **多返回值支持**：可返回单个值或多个值（包括 `nil`、基本类型、字符串、Java 对象）。
- **错误隔离**：任务中抛出的异常会被捕获并转为错误字符串返回，不会导致整个进程崩溃。
- **对象池管理**：Java 对象可通过 ID 在任务间传递和复用。
- **高效轮询**：通过 `checkPromise` 轻量级检查任务完成状态，轮询开销极低。

---

## 核心概念

### Promise（承诺）
Promise 是一个**任务凭证**，由 `java.promise()` 创建，返回一个整数 ID。该 ID 关联一个后台任务，用于查询状态和获取结果。

### 任务生命周期
1. 创建 Promise ID。
2. 调用 `java.runAsync` 或 `java.runAsyncObj` 提交任务，传入 ID、目标类/对象、方法名和参数。
3. 任务在线程池中执行，完成后结果存储在 Promise 中。
4. 消费结果（二选一）：
   - **轮询**：`java.checkPromise(id)`，若完成则获取返回值，并自动清理该 Promise。
   - **回调**：`java.onComplete(id, callback)`，任务完成时后台线程自动调用回调，并自动清理该 Promise。

### 线程池
- 底层由 `LuaAgent` 管理，基于 `ThreadPoolExecutor`。
- 核心线程数 = `max(CPU核心数 * 2, 4)`，所有线程为守护线程。
- 任务超时默认为 300 秒，可配置（通过 `submitTaskFuture` 的重载，但目前 Lua API 未暴露超时参数，可根据需要扩展）。

---

## Lua API 参考

### `java.promise()`
创建一个新的 Promise，返回一个整数 ID。
```lua
local id = java.promise()   -- 返回整数，如 42
```

### `java.runAsync(id, class_name, method_name, ...)`
提交一个**静态方法**异步任务。

**参数**：
- `id` (integer)：由 `java.promise()` 返回的 Promise ID。
- `class_name` (string)：类的全限定名，如 `"java.lang.Integer"`。
- `method_name` (string)：方法名。特殊方法名 `"new"` 表示调用构造器。
- `...` (可变参数)：传递给 Java 方法的参数。每个参数在 Lua 中提供**值和类型提示**（自动推断），底层会按顺序匹配。

**返回值**：无（任务提交后立即返回）。

**示例**：
```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
-- 稍后检查结果
```

### `java.runAsyncObj(id, obj, method_name, ...)`
提交一个**实例方法**异步任务。

**参数**：
- `id` (integer)：Promise ID。
- `obj` (userdata)：Java 对象实例（通过 `java.import` 构造或从之前异步任务返回）。
- `method_name` (string)：实例方法名。
- `...` (可变参数)：方法参数。

**示例**：
```lua
local String = java.import("java.lang.String")
local s = String:new("Hello World")
local id = java.promise()
java.runAsyncObj(id, s, "length")
```

### `java.checkPromise(id)`
检查 Promise 是否完成，并获取结果。

**行为**：
- 若任务**未完成**：返回 `false, nil`。
- 若任务**已完成**：返回 `true, ...`，其中 `...` 是任务返回的一个或多个值。如果任务执行出错，返回值将是一个以 `"E:"` 开头的错误字符串。

**注意**：调用 `checkPromise` 后，若任务已完成，该 Promise 会被自动清理（从注册表中移除），后续再次调用将返回 `false, nil`。

**示例**：
```lua
local done, result = java.checkPromise(id)
if done then
    print("Result:", result)
else
    print("Still running...")
end
```

### `java.onComplete(id, callback)`
为 Promise 注册**完成回调**，任务完成时由后台工作线程自动调用，无需轮询。

**参数**：
- `id` (integer)：Promise ID。
- `callback` (function)：完成回调，签名 `callback(err, result...)`。
  - `err == nil`：任务成功，`result...` 与 `checkPromise` 返回的结果值**完全一致**（含多返回值、`O:` 对象 id、`nil`）。
  - `err == string`：任务失败，内容即 `E:` 前缀后的错误信息，格式为 `类名.方法 -> 异常类型: 消息`（如 `"java.lang.NonExistent.foo -> ClassNotFoundException: java.lang.NonExistent"`），已包含根因。

**行为**：
- 若任务**尚未完成**：注册回调，任务完成后自动触发一次。
- 若任务**已完成**（先完成再注册）：立即触发。
- 同一 id 重复注册回调：后者覆盖前者。
- 回调触发后，该 Promise 会被自动清理（与 `checkPromise` 消费后的清理一致）。

**示例**：
```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
java.onComplete(id, function(err, result)
    if err then
        print("失败:", err)
    else
        print("结果:", result)   -- 42
    end
end)
```

> **注意**：回调在后台工作线程（LuaAgent 线程池）上执行，应**快速返回**；耗时的重活在回调里应再通过 `runAsync` 提交新任务，不要在回调中阻塞线程池。

**设计说明**：
- 取名 `onComplete` 而非 `then`：`then` 是 Lua 保留关键字，无法直接作为字段名访问（`java.then(...)` 会报语法错误）。
- 三种消费原语并存，按需选用：

| 原语 | 机制 | 适用场景 |
|---|---|---|
| `java.checkPromise(id)` | 轮询 | 主流程 / REPL 循环检查，最直观 |
| `java.onComplete(id, cb)` | 回调 | 事件驱动、不想占用主线程 |
| `java.await(id)` | 协程 `lua_yield`/`lua_resume` | 协程内顺序等待（`PromiseTest` 依赖，保留） |

- 回调与轮询**结果语义完全一致**（共享同一套结果解析函数），两种方式可放心混用或切换。
- 同一 id **混用** `onComplete` 与 `checkPromise` 是允许的：两者读到的是同一份结果，但**清理只发生一次**——回调触发后或轮询消费后该 Promise 即从注册表移除，后续访问返回 `false, nil`。建议一个 id 只用一种方式。
- 回调在后台工作线程上运行，内部经 `lua_mutex`（递归锁）串行访问 Lua 状态；锁序固定为"先释放 `promise_mutex`，再获取 `lua_mutex`"，与主线程路径无反向持锁，不会死锁。回调内再调用 Java（含 `runAsync`）可重入，安全。

### `java.yield(ms)`
短暂释放 Lua 互斥锁（`lua_mutex`），等待约 `ms` 毫秒（默认 10）后重新获取。

**用途**：主线程在轮询等待期间让出 Lua 锁，使后台工作线程（LuaAgent 线程池）有机会执行已注册的 `onComplete` 回调或代理回调，避免"主线程持锁等待 → 工作线程无法执行 Lua 回调"的死锁。注意这与 `Thread.sleep` 不同——`Thread.sleep` 不释放 Lua 锁。

**示例**：
```lua
local done = false
java.onComplete(id, function(err, result)
    done = true
end)
while not done do
    java.yield(10)   -- 释放 Lua 锁，让回调得以执行
end
```

### `java.getObject(id)`
从对象池中获取一个 Java 对象（userdata），该对象通常由异步任务返回（如构造器返回的对象）。

**参数**：`id` (integer) – 对象在池中的注册 ID（从 `checkPromise` 返回的 `"O:数字"` 格式中解析）。

**返回值**：Java 对象的 Lua userdata，可像普通 Java 对象一样调用其方法。

**示例**：
```lua
local done, oid = java.checkPromise(id)
if done and type(oid) == "string" and oid:match("^O:") then
    local obj_id = tonumber(oid:sub(3))
    local obj = java.getObject(obj_id)
    print(obj:length())   -- 调用实例方法
end
```

---

## 类型转换与序列化

当异步任务返回一个 Java 对象时，它会被序列化为以下格式之一，并通过 `checkPromise` 返回：

| Lua 类型 / 格式          | Java 返回类型                     |
|--------------------------|----------------------------------|
| `nil`                    | `null` / `void`                  |
| 字符串（如 `"hello"`）   | `java.lang.String`               |
| 整数（如 `42`）          | `int` / `Integer` / `long` / `Long` |
| 浮点数（如 `3.14`）      | `double` / `Double` / `float` / `Float` |
| 布尔（`true` / `false`） | `boolean` / `Boolean`            |
| `O:数字`（如 `"O:123"`） | 任意其他 Java 对象（自动注册到对象池） |
| `E:错误信息`             | 异常（任务执行失败）             |

**多返回值**：若 Java 方法返回 `Object[]` 或多个值（通过 `callFunctionMultiple` 模拟），则 `checkPromise` 会返回多个值。

---

## 示例场景

### 1. 异步执行静态方法（解析整数）
```lua
local java = require("java")
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "12345")

-- 轮询直到完成
repeat
    local done, result = java.checkPromise(id)
until done

print(result)  -- 输出 12345（整数）
```

### 2. 异步调用实例方法
```lua
local String = java.import("java.lang.String")
local s = String:new("Hello, Agent!")

local id = java.promise()
java.runAsyncObj(id, s, "toUpperCase")

repeat
    local done, result = java.checkPromise(id)
until done

print(result)  -- 输出 "HELLO, AGENT!"
```

### 3. 异步构造对象
```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Constructed in background")

repeat
    local done, result = java.checkPromise(id)
until done

-- result 形如 "O:42"，需要解析并获取对象
local obj_id = tonumber(result:sub(3))
local obj = java.getObject(obj_id)
print(obj:length())  -- 输出 26
```

### 4. 处理错误
```lua
local id = java.promise()
java.runAsync(id, "java.lang.NonExistent", "foo", "arg")  -- 类不存在

repeat
    local done, result = java.checkPromise(id)
until done

-- result 将以 "E:" 开头
print(result)  -- 输出 "E:java.lang.ClassNotFoundException: java.lang.NonExistent"
```

### 5. 并发执行多个任务
```lua
local function async_task(n)
    local id = java.promise()
    java.runAsync(id, "java.lang.Thread", "sleep", tostring(n))
    return id
end

local ids = { async_task(100), async_task(200), async_task(300) }

for _, id in ipairs(ids) do
    repeat
        local done = java.checkPromise(id)
    until done
    print("Task", id, "completed")
end
```

---

## Java 侧扩展（高级）

如果你需要从 Java 侧提交任务，可以使用 `LuaAgent` 类：

### `LuaAgent.submitTask(AgentTask task)`
提交一个 `AgentTask` 对象，任务结果将通过 JNI 回调 `complete(int pid, String result)` 返回。

### `AgentTask` 构造
- **静态方法**：`new AgentTask(pid, className, methodName, args)`  
  `args` 为字符串数组，每两个元素为一对：`[value, hint, value, hint, ...]`，`hint` 为类型提示（`"S"` 字符串，`"I"` 整数等）。
- **实例方法**：`new AgentTask(pid, instance, methodName, args)`

### `LuaAgent.getObject(int id)` / `registerObject(Object obj)`
管理对象池，用于跨任务传递 Java 对象。

---

## 注意事项

1. **线程安全**：`LuaAgent` 的线程池是**共享的**，所有 `LuaRuntime` 实例共用。关闭单个 `LuaRuntime` 不会关闭线程池，需要手动调用 `LuaAgent.shutdown()`（或进程退出时自动清理）。
2. **对象生命周期**：通过 `getObject` 获取的对象是 `userdata`，受 Lua GC 管理。若对象被回收，对应的 JVM 全局引用会释放。
3. **超时处理**：当前 Lua API 未暴露超时参数，但底层默认超时 300 秒。若需自定义，可扩展 `java.runAsync` 增加超时参数。
4. **性能开销**：每次 `checkPromise` 都会进行互斥锁操作和哈希查找，但开销极小（~微秒级），可高频轮询。
5. **返回值限制**：当前实现不支持直接返回 `byte[]` 或复杂嵌套对象，如需传递大数据，建议序列化为字符串或使用共享内存。
6. **协程交互**：Promise 机制与 Lua 协程兼容，你可以在协程中轮询，不会阻塞其他协程。

---

## 内部实现简述

- **Lua 侧**：`java.promise()` 在 C 层创建 `PromiseEntry`，存入全局链表，返回递增 ID。
- **任务提交**：`java.runAsync` 将参数打包为字符串数组，调用 Java 的 `LuaAgent.submitTask`。
- **Java 执行**：`AsyncRunner` 通过反射匹配方法，将 Lua 参数转换为 Java 类型，执行方法，并将结果序列化为字符串。
- **结果回调**：`LuaAgent.complete`（JNI 函数）将结果存入 `PromiseEntry`，并置 `done` 标志。
- **轮询检查**：`java.checkPromise` 遍历链表，若任务完成则解析结果并弹出栈。

---

## 常见问题

**Q: 如何传递多个参数？**  
A: 直接在 `runAsync` 后面依次传入即可，例如 `java.runAsync(id, "Math", "max", "10", "20")`。

**Q: 返回的对象如何调用方法？**  
A: 使用 `java.getObject` 获取 userdata，然后像普通 Java 对象一样调用，例如 `obj:method(args)`。

**Q: 任务执行时间太长怎么办？**  
A: 可在提交时传入超时参数（需自定义），或通过 `LuaAgent` 的 `cancelTask` 方法取消任务（目前 Lua API 未暴露，可自行扩展）。

**Q: 能否在任务中返回多个值？**  
A: 可以，Java 方法若返回 `Object[]`，`checkPromise` 会返回多个值。但当前实现中，大多数 Java 方法只返回单值，多返回值常用于 Lua 函数回调。

**Q: 如何调试异步任务？**  
A: 可在任务中打印日志（通过 `System.out.println`），或使用 `java.runAsync` 执行一个简单的测试方法，观察 `checkPromise` 返回的错误信息。

---

## 版本历史

- **v2.2.5**：新增回调式消费 API `java.onComplete(id, callback)`，与 `checkPromise` 轮询并存，用户按需二选一；新增 `java.yield(ms)` 等待原语（释放 Lua 锁）；统一 PromiseEntry 生命周期清理。
- **v2.0**（当前）：初始版本，支持静态/实例异步调用、对象池、多返回值。
- **未来计划**：支持任务取消、超时控制、自定义线程池配置。

---

## 相关文件

- `java/src/com/luajava/LuaAgent.java` – 线程池与任务管理
- `java/src/com/luajava/AsyncRunner.java` – 反射调用与序列化
- `java/src/com/luajava/AgentTask.java` – 任务数据载体
- `native/lualib/lualib_async.c` – Lua C 绑定实现
- `native/jni/luajava.c` – JNI 回调实现

---

*文档最后更新：v2.2.5*