![Lua](https://img.shields.io/badge/Lua-5.4.8-blue) ![Java](https://img.shields.io/badge/Java-17%2B-orange) ![License](https://img.shields.io/badge/license-MIT-green)

# LuaJavaNE

**Lua 5.4.8 + Java 双向互调引擎**。在 Lua 里直接调用任何 Java 类库，在 Java 里执行 Lua 脚本。支持高性能的 C 端数学运算库与异步多线程 Agent 框架。

---

## 📑 目录

- [快速开始](#quick-start)
- [特性](#features)
- [API 参考](#api-reference)
  - [Java 调用 Lua](#java-call-lua)
  - [Lua 调用 Java](#lua-call-java)
  - [注解绑定](#annotation-bind)
  - [动态代理](#dynamic-proxy)
  - [数组操作](#array-ops)
  - [高性能缓存 store/fetch](#store-fetch)
  - [Agent 异步框架](#agent-async)
  - [Clac 数学加速库](#clac-math)
- [性能对比](#performance)
- [构建与测试](#build-test)
- [架构概览](#architecture)
- [已知限制](#limitations)
- [贡献](#contributing)
- [许可证](#license)

## 🚀 快速开始 {#quick-start}

### 从源码构建

```bash
git clone https://github.com/npp-zep/LuaJavaNE.git
cd LuaJavaNE
make
./luaj.sh
```

从 Release 下载

从 Releases 下载 luajava.jar 和 luajava.so（ARM64 用户下载 luajava-arm64.so 并重命名为 luajava.so），放同一目录：

```bash
java -Dluajava.library.path=./luajava.so -jar luajava.jar
```

命令行

```bash
luaj                    # 启动交互式 REPL
luaj script.lua         # 执行脚本文件
luaj -e "print(1+1)"    # 执行一行代码
luaj -v                 # 版本信息
luaj -h                 # 帮助
```

REPL 快捷键

快捷键 功能
← → 左右移动光标
↑ ↓ 浏览输入历史
Ctrl+R 搜索历史
Ctrl+A 跳到行首
Ctrl+E 跳到行尾
Ctrl+C 取消当前输入
Ctrl+D 退出 REPL
\q 退出 REPL
= 1+2 打印表达式结果

✨ 特性

· 双向互调：Java 执行 Lua 脚本，Lua 调用 Java 类库，无缝集成。
· 高性能缓存：java.store/fetch 直接操作 C 链表，绕开 JNI 类型转换。
· Agent 异步框架：后台线程池执行 Java 方法，Promise 回传结果，不阻塞主线程。
· 批量数学运算：ClacArray 提供原生 double 数组操作，比 Lua 表循环快 83 倍。
· 注解绑定：@LuaModule / @LuaFunction 自动将 Java 模块暴露给 Lua。
· 动态代理：Lua 表实现 Java 接口，作为回调传给 Java。
· 类型自动映射：Lua ↔ Java 基本类型自动转换，支持多返回值。

📖 API 参考

Java 调用 Lua

```java
LuaRuntime L = new LuaRuntime();
L.doString("function add(a, b) return a + b end");
Object result = L.callFunction("add", 3, 5); // 8

LuaFunctionObj fn = L.compile("return function(x) return x * 2 end");
LuaFunctionObj doubler = (LuaFunctionObj) fn.call();
doubler.call(21); // 42
fn.destroy();
doubler.destroy();
L.close();
```

Lua 调用 Java

```lua
local java = require("java")
local String = java.import("java.lang.String")
local s = String:new("Hello World")
print(s:length())          -- 11
print(s:substring(0, 5))   -- Hello
```

注解绑定

```java
@LuaModule("math")
class MyMath {
    @LuaFunction public int add(int a, int b) { return a + b; }
    @LuaFunction("multiply") public int mul(int a, int b) { return a * b; }
}
LuaRuntime L = new LuaRuntime();
L.registerModule(new MyMath());
L.doString("print(math_add(3, 5))");      // 8
L.doString("print(math_multiply(6, 7))"); // 42
```

动态代理

用 Lua 表实现 Java 接口，作为回调传给 Java 方法：

```lua
local Runnable = java.import("java.lang.Runnable")
local Thread = java.import("java.lang.Thread")
local handler = { run = function(self) print("Hello from Lua!") end }
local proxy = java.createProxy({"java.lang.Runnable"}, handler)
Thread:new(proxy):start()
```

数组操作

```lua
local arr = java.newArray("int", 3)
arr[0] = 100; arr[1] = 200; arr[2] = 300
print(#arr)      -- 3
print(arr[1])    -- 200
```

高性能缓存 store/fetch

直接操作 C 端键值存储，避免 JNI 类型转换，适合高频共享数据。

```lua
java.store("counter", 0)
for i = 1, 1000 do
    local v = java.fetch("counter")
    java.store("counter", v + 1)
end
```

Agent 异步框架

异步调用静态方法

```lua
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
-- 主线程继续...
local done, result = java.checkPromise(id) -- 轮询结果
if done then print(result) end -- 42
```

异步调用实例方法

```lua
local String = java.import("java.lang.String")
local s = String:new("Hello World")
local id = java.promise()
java.runAsyncObj(id, s, "length")
repeat done, len = java.checkPromise(id) until done
print(len) -- 11
```

异步构造对象

```lua
local id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello Async")
repeat done, oid = java.checkPromise(id) until done
local obj = java.getObject(oid) -- 获取 userdata
print(obj:length()) -- 11
```

错误处理

```lua
local id = java.promise()
java.runAsync(id, "java.lang.NoSuchClass", "foo", "")
repeat done, err = java.checkPromise(id) until done
print(err) -- "java.lang.ClassNotFoundException: java.lang.NoSuchClass"
```

多返回值

```lua
local s = String:new("a,b,c")
local id = java.promise()
java.runAsyncObj(id, s, "split", ",")
local done, a, b, c = java.checkPromise(id)
print(a, b, c) -- a   b   c
```

并发能力

线程池大小 = CPU 核心数 × 2，daemon 线程，自动退出。50 个并发 Thread.sleep(10ms) 总耗时 ~0.05 秒。

Clac 数学加速库

```lua
local clac = require("clac")
print(clac.sin(clac.pi() / 2))  -- 1.0
print(clac.log2(8))              -- 3.0
print(clac.erf(1))               -- 0.8427...

-- 批量运算：ClacArray
local a = clac.array(100000)
local b = clac.array(100000)
for i = 1, 100000 do a[i] = math.random(); b[i] = math.random() end
local c = clac.batch_add(a, b)   -- 比 Lua 表循环快 83 倍
local s = clac.batch_sin(a)      -- 批量正弦
```

⚡ 性能对比

测试项 耗时 倍数
10000×100 Lua table add 0.404s 1x
10000×100 ClacArray batch_add 0.005s 83x faster
50 并发 Thread.sleep(10ms) 0.05s -
100 次异步调用 0.43s -

🔧 构建与测试

构建

需要 JDK 17+、CMake 3.14+、GCC/Clang、pthread。

```bash
git clone https://github.com/npp-zep/LuaJavaNE.git
cd LuaJavaNE
make
```

测试

```bash
make test
```

JUnit 测试覆盖：基本互调、数组、代理、注解、异步静态/实例/构造/错误/多返回值/并发、Promise 等，共 28 个测试用例，全部通过。

项目结构

```
LuaJavaNE/
├── Lua5.4.8/          # Lua 5.4.8 源码 + 自定义库
├── jni/               # JNI 桥接 C 代码
├── src/               # Java 公共 API
├── test/              # JUnit 测试
├── docs/              # 文档
├── build/             # 编译产物
├── CMakeLists.txt     # C 构建
├── Makefile           # 一键构建/测试
└── luaj.sh            # 启动脚本
```

🏗 架构概览

```
Java (LuaRuntime / 注解 / Agent)
   ↕ JNI (luajava.c, 持全局锁 lua_mutex)
C 层 (lualibjava.c / lualib_async.c / lualib_clac.c)
   ↕ Lua C API
Lua 5.4.8 内核
```

· Java → Lua：doString / callFunction → JNI → luaL_dostring / lua_pcall，全程持锁。
· Lua → Java：java.import / 对象:方法() → C 元方法 → JNI 反射。
· 高性能路径：store/fetch 直接读写 C 链表；ClacArray 在 C 内存中完成批量运算。
· 线程约束：所有 Lua 执行必须在主线程。异步任务通过 Java 线程池执行纯 Java 方法，结果写入 C 链表，主线程轮询。

⚠️ 已知限制

· 异步方法目前仅支持单个参数（多参数将在后续版本支持）
· 方法重载匹配可能不精确（自动匹配第一个兼容签名）
· Termux/Android 环境下线程数上限约 200（受系统限制）
· LuaRuntime.compile() 在 x86_64 Linux 上可能崩溃（ARM64 正常）
· 代理对象直接 print() 可能段错误，用 tostring(proxy) 替代
· 类型 long / float / byte / char 暂不完全支持

🤝 贡献

欢迎 Issue 和 PR。

```bash
git clone https://github.com/npp-zep/LuaJavaNE.git
cd LuaJavaNE
git checkout -b feature/my-feature
# 修改代码
make && make test
git commit -m "Description"
git push origin feature/my-feature
# 在 GitHub 上提 Pull Request
```

📄 许可证

MIT。本项目包含 Lua 5.4.8 (MIT)、JLine 3 (BSD-3)、JUnit 5 (EPL-1.0)。

---

