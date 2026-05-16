
# LuaJavaNE Agent v2 异步 API 文档

## 概述
Agent v2 提供多线程异步调用 Java 方法的能力。所有异步任务在后台线程池中执行，结果通过 Promise 机制回传主线程。

## 核心 API

### 1. 异步调用静态方法
```lua
local id = java.promise()
java.runAsync(id, "类名", "方法名", 参数1, 参数2, ...)
```

示例：

```lua
-- 调 Integer.parseInt("42")
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
```

2. 异步调用实例方法

```lua
-- 先创建对象
local String = java.import("java.lang.String")
local s = String:new("Hello World")

-- 异步调实例方法
local id = java.promise()
java.runAsyncObj(id, s, "length")
```

3. 异步构造对象

```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello")

-- 获取对象 ID 并转为 userdata
local oid = await(id)  -- 返回数字 ID
local obj = java.getObject(oid)  -- 转为 Java.Object
print(obj:length())  -- 5
```

4. 轮询结果（同步等待）

```lua
-- 方式一：轮询
while true do
    local done, result = java.checkPromise(id)
    if done then
        print(result)
        break
    end
    java.import("java.lang.Thread"):sleep(10)
end

-- 方式二：阻塞轮询
repeat
    local done, result = java.checkPromise(id)
until done
print(result)
```

5. 多返回值

```lua
-- String.split 返回数组
local s = String:new("a,b,c")
local id = java.promise()
java.runAsyncObj(id, s, "split", ",")

repeat
    local done, a, b, c = java.checkPromise(id)
until done
print(a, b, c)  -- a   b   c
```

并发性能

· 线程池：CPU 核心数 × 2，daemon 线程
· 50并发 sleep(10ms)：0.05秒
· 100次快速调用：0.43秒

支持的类型

Lua 类型 Java 类型 方向
integer int / long 双向
number double / float 双向
string String 双向
boolean boolean 双向
nil void / null 返回

错误处理

异步任务抛异常时，checkPromise 返回错误字符串：

```lua
-- 类不存在 → "java.lang.ClassNotFoundException: xxx"
-- 方法不存在 → "no matching method: xxx"
-- 参数类型错误 → "java.lang.NumberFormatException: xxx"
```

生命周期

```lua
-- 创建
local id = java.promise()

-- 提交任务
java.runAsync(id, ...)

-- 轮询结果
local done, result = java.checkPromise(id)

-- 任务完成，Promise 自动释放
-- 关闭虚拟机时线程池自动 shutdown
L:close()
```

限制

· 不支持方法重载（自动匹配第一个兼容签名）
· 单参数上限 1 个（复杂参数需先构造对象）
· Termux/Android 环境下线程数上限约 200
· 不支持取消正在执行的任务
  EOF
