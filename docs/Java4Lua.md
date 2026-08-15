# LuaJavaNE - Lua 端调用 Java API 参考手册

本文档面向使用 **LuaJavaNE** 的 Lua 开发者，介绍如何在 Lua 脚本中无缝调用 Java 类库、对象、数组和异步任务。

> 所有示例均假设已通过 `local java = require("java")` 加载了 `java` 模块。

---

## 1. 加载 Java 模块

在 Lua 脚本中，首先需要引入 `java` 模块：

```lua
local java = require("java")
```

该模块提供了所有与 Java 交互的入口。

---

## 2. 导入 Java 类

使用 `java.import(类全名)` 获取 Java 类的引用（相当于 Java 的 `Class` 对象）。

```lua
local String = java.import("java.lang.String")
local ArrayList = java.import("java.util.ArrayList")
local HashMap = java.import("java.util.HashMap")
local System = java.import("java.lang.System")
```

**注意**：类名必须使用全限定名（包名 + 类名），例如 `"java.lang.String"`。

---

## 3. 创建 Java 对象（调用构造器）

导入的类本身可被当作**构造函数**调用，使用 `类:new(参数...)` 或直接 `类(参数...)` 创建实例。

```lua
-- 两种写法等价
local s1 = String:new("Hello")
local s2 = String("World")          -- 直接调用

local list = ArrayList()
list:add("a")
list:add("b")
```

如果构造器需要多个参数，直接传入即可，Lua 类型会自动转换为对应的 Java 类型（见后文“类型映射”）。

---

## 4. 调用实例方法

通过 `对象:方法名(参数...)` 调用实例方法。

```lua
local s = String("LuaJavaNE")
print(s:length())          -- 输出: 9
print(s:substring(0, 4))   -- 输出: "LuaJ"
print(s:indexOf("Java"))   -- 输出: 3
```

**重载方法**：LuaJavaNE 会根据参数类型和数量自动匹配最合适的重载版本。

---

## 5. 调用静态方法

通过 `类.方法名(参数...)` 调用静态方法。

```lua
local System = java.import("java.lang.System")
local Math = java.import("java.lang.Math")

-- 获取系统属性
local version = System:getProperty("java.version")   -- 注意：静态方法也使用冒号调用，但第一个参数是类本身
print(version)

-- 数学函数
local pi = Math:PI()        -- 获取常量（静态字段）
local maxVal = Math:max(10, 20)
```

**注意**：静态方法和静态字段都通过 `类:字段/方法` 访问（使用冒号，但底层自动识别静态性）。

---

## 6. 访问和修改字段

### 6.1 实例字段

直接使用 `对象.字段名` 读取或赋值。

```lua
local p = java.import("java.awt.Point"):new(10, 20)
print(p.x)   -- 输出 10
p.x = 100
print(p.x)   -- 输出 100
```

### 6.2 静态字段

通过类对象访问。

```lua
local Math = java.import("java.lang.Math")
print(Math.PI)   -- 输出 3.1415926535898
```

同样可赋值（如果字段不是 final）。

> **注意**：对象访问时**方法优先于字段**——当对象同时存在同名方法和字段时，`对象:名字` 会调用方法而非读取字段（如 `BigDecimal.scale()` 调用的是方法，而不是读取其私有 int 字段）。仅当不存在同名方法时才按字段读取。

---

## 7. 创建 Java 数组

使用 `java.newArray(类型名, 长度)` 创建基本类型或对象数组。

支持的 `类型名`：
- `"int"` / `"java.lang.Integer"`
- `"double"` / `"java.lang.Double"`
- `"boolean"` / `"java.lang.Boolean"`
- `"String"` / `"java.lang.String"`
- 其他对象类型（如 `"java.util.Date"`）

```lua
-- 创建 int 数组
local intArr = java.newArray("int", 5)
intArr[1] = 10   -- 索引从 1 开始（Lua 风格）
intArr[2] = 20
print(intArr[1]) -- 输出 10
print(#intArr)   -- 输出 5（数组长度）

-- 创建 String 数组
local strArr = java.newArray("String", 3)
strArr[1] = "a"
strArr[2] = "b"
```

**注意**：数组索引从 **1** 开始（遵循 Lua 习惯），而非 Java 的 0。

---

## 8. 动态代理（Lua 表实现 Java 接口）

使用 `java.createProxy({接口名列表}, handler表)` 创建 Java 代理对象，其中 `handler` 表需包含对应接口方法的 Lua 函数。接口方法被调用时派发到 handler 表中**同名（区分大小写）**的 Lua 函数，第一个参数 `self` 为 handler 表。

```lua
local ran = false
local proxy = java.createProxy({"java.lang.Runnable"}, {
    run = function(self) ran = true end
})

-- 在后台线程中运行
local Thread = java.import("java.lang.Thread")
local t = Thread:new(proxy)
t:start()

-- 注意：不要在持有 Lua 锁期间调用 t:join()——主线程持锁等待，
-- 而后台线程的 run() 回调也需要这把锁，会互相等待导致死锁。
-- 等待期用 java.yield 短暂释放 Lua 锁，子线程才有机会执行回调。
local waited = 0
while not ran and waited < 3000 do
    java.yield(10)
    waited = waited + 10
end
```

**类型转换**：
- 参数：`String`、整数、浮点、布尔转换为对应的 Lua 值；其他 Java 对象参数目前以 `nil` 传入。
- 返回值：`String`、整数、浮点、布尔、`null` 转换为对应的 Lua 值；其他对象包装为 userdata（可继续调用其方法）。

完整可运行示例见 `examples/proxy.lua`。

---

## 9. 异步任务 API（Agent V2）

LuaJavaNE 提供了一套基于 **Promise** 的异步任务系统，可在后台线程池执行 Java 方法，结果通过轮询取回。

### 9.1 创建 Promise

```lua
local id = java.promise()   -- 返回一个整数 ID
```

每个 `id` 关联一个可等待的异步结果。

### 9.2 提交静态方法任务

```lua
java.runAsync(id, "类名", "方法名", 参数1, 参数2, ...)
```

示例：
```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "123")
```

### 9.3 提交实例方法任务

```lua
java.runAsyncObj(id, 对象, "方法名", 参数1, 参数2, ...)
```

示例：
```lua
local s = java.import("java.lang.String")("Hello")
local id = java.promise()
java.runAsyncObj(id, s, "length")
```

### 9.4 构造对象并异步返回

通过 `"new"` 方法名构造对象：
```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello World")
```

### 9.5 轮询结果

```lua
local done, result1, result2, ... = java.checkPromise(id)
```

- `done`：布尔值，`true` 表示已完成。
- 后续参数是返回值（可能是多个）。

**轮询示例**：
```lua
repeat
    local done, val = java.checkPromise(id)
until done
print(val)   -- 打印 "123"
```

### 9.6 获取异步构造的对象

如果异步任务返回一个 Java 对象（例如构造器返回），`checkPromise` 会返回一个 **对象 ID**（整数），你需要通过 `java.getObject(id)` 将其转换为 Lua 可用的 Java 对象 userdata。

```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "AsyncString")
repeat
    local done, oid = java.checkPromise(id)
until done
local obj = java.getObject(oid)   -- obj 现在是一个 Java String 对象
print(obj:length())               -- 输出 11
```

### 9.7 错误处理

如果异步任务抛出异常，`checkPromise` 会返回错误字符串（以 `"E:"` 开头）。

```lua
local id = java.promise()
java.runAsync(id, "java.lang.NonExistentClass", "foo")
local done, err = java.checkPromise(id)
if err and string.sub(err, 1, 2) == "E:" then
    print("Error: " .. string.sub(err, 3))
end
```

### 9.8 回调消费结果（java.onComplete）

除轮询外，也可注册完成回调，任务完成时由后台线程自动调用，无需轮询：

```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
java.onComplete(id, function(err, result)
    if err then
        print("失败:", err)      -- err 为错误信息字符串
    else
        print("结果:", result)   -- 42
    end
end)
```

- 回调签名：`callback(err, result...)`，`err == nil` 表示成功，`result...` 与 `checkPromise` 返回值一致。
- 若任务已完成再注册，会立即触发。
- 回调在后台工作线程执行，应**快速返回**；耗时的重活请再次 `runAsync` 提交。

### 9.9 释放锁等待（java.yield）

主线程轮询等待异步结果或代理回调时，`java.yield(ms)` 会短暂释放 Lua 锁（默认 10ms）再重新获取，让后台工作线程有机会执行回调，避免"主线程持锁等待 → 工作线程无法执行 Lua"的死锁（详见第 8 节）。

```lua
while not done do
    java.yield(10)          -- 默认 10ms
end
```

---

## 10. 跨 Lua 状态的全局存储（java.store / java.fetch）

`java.store` 和 `java.fetch` 提供了跨多个 `LuaRuntime` 实例共享数据的机制（基于进程内全局哈希表）。

```lua
-- 存储
java.store("myKey", 42)
java.store("greeting", "Hello")

-- 读取
local val = java.fetch("myKey")   -- 返回 42
local msg = java.fetch("greeting") -- 返回 "Hello"

-- 删除
java.deleteStore("myKey")
```

支持的类型：`nil`, `number`, `string`, `boolean`。存储的值会在进程生命周期内保留（除非显式删除）。

---

## 11. 类型映射（Lua ↔ Java）

| Lua 类型      | Java 类型                     | 说明                               |
|---------------|-------------------------------|------------------------------------|
| `nil`         | `null`                        | 传递空引用                         |
| `boolean`     | `boolean` / `java.lang.Boolean` | 自动拆装箱                        |
| `number` (整数) | `int`, `long`（视范围）      | Lua 整数若超出 int 范围则用 long   |
| `number` (浮点) | `double` / `float`           | 浮点数优先作为 double，可匹配 float |
| `string`      | `java.lang.String`            | 自动转换                           |
| `table` (用作参数) | 不支持直接传递，需用代理或数组 | 若需传递 Lua 表给 Java，请使用 `java.createProxy` |
| `userdata` (Java 对象) | 对应 Java 对象            | 保持原引用                         |
| `function`    | 不直接支持，可用代理封装       | 可通过 `java.createProxy` 包装为接口 |

**返回值转换**：
- Java `void` → Lua `nil`
- Java `String` → Lua `string`
- Java 基本类型（int, double, boolean 等）→ 对应的 Lua 类型
- Java 对象 → Lua userdata（可继续调用其方法）

---

## 12. 注意事项

1. **线程安全**：异步任务在独立线程池执行，但 Lua 状态本身不是线程安全的。请勿在多个线程中同时操作同一个 `LuaRuntime` 实例（除非外部加锁）。

2. **方法重载**：LuaJavaNE 会根据参数类型和数量匹配最合适的重载版本；常规签名查找失败时会自动回退到 Java 反射调用（支持任意返回类型，如 `java.math.BigInteger`/`BigDecimal` 的方法），仍无法匹配才抛出 Lua 错误。

3. **资源释放**：Java 对象由 JVM GC 管理，但 Lua userdata 会持有 JNI 全局引用，应避免大量临时对象造成内存压力。必要时可显式调用 `java.import("java.lang.System"):gc()` 建议 GC。

4. **数组索引**：Java 数组在 Lua 中索引从 1 开始，与 Lua 惯例一致。

5. **异步超时**：目前没有提供超时机制，可在 Lua 侧用 `utils.timer` 自行实现。

6. **调试**：可使用 `java.toString(对象)` 获取 Java 对象的字符串表示（等同于 Java 的 `toString()`）。

---

## 13. 完整示例

```lua
local java = require("java")

-- 导入类
local String = java.import("java.lang.String")
local ArrayList = java.import("java.util.ArrayList")
local System = java.import("java.lang.System")

-- 创建对象
local s = String("Hello from Lua!")
print(s:length())   -- 17

-- 静态方法
print(System:currentTimeMillis())

-- 数组
local arr = java.newArray("int", 3)
arr[1] = 10
arr[2] = 20
arr[3] = 30
for i = 1, #arr do print(arr[i]) end

-- 异步调用
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
local done, result
repeat
    done, result = java.checkPromise(id)
until done
print("Async result:", result)   -- 42

-- 全局存储
java.store("counter", 100)
print(java.fetch("counter"))     -- 100
```

---

## 14. 更多资料

- 项目主页：https://github.com/npp-zep/LuaJavaNE
- Java 侧 API 文档（LuaRuntime 等）见 `docs/` 目录或源码注释。

---
*本文档对应 LuaJavaNE 版本 2.2.5。*