LuaJavaNE API 参考（main 分支）

文档覆盖 main 分支（稳定版）所有公开接口、传参方式、使用场景与已知限制。
内部函数、调试用函数不在列。如需完整源码，请直接查看对应文件。

---

1. Lua 侧 java 模块

所有 Lua API 均通过 require "java" 获得。

1.1 类导入与对象创建

java.import(className)

· 参数：className 字符串，Java 类全名（如 "java.lang.String"）。
· 返回值：代表 Java 类的 userdata，支持 .new / 静态方法 / 静态字段。
· 使用场景：一切 Java 操作的入口。
· 限制：
  · 不支持数组类（int[]）。需使用 java.newArray。
  · 类名大小写敏感。

类:new(...)

· 说明：调用 Java 构造方法创建对象。
· 传参：按 Java 构造方法的参数类型匹配，支持重载（优先匹配 String > int > boolean > double > long > void > Object）。
· 返回值：表示 Java 对象的 userdata。
· 局限：方法重载解析不精确，复杂重载可能误匹配。

对象/类的字段访问

· 读：obj.field / Class.field。
· 写：obj.field = value / Class.field = value。
· 支持类型：String、int、double、boolean、long，其他对象字段。
· 局限：静态字段写入仅对非 final 字段有效；字段不存在时返回方法查找器，不会报错（可能导致后续调用 nil）。

对象/类的方法调用

· 实例方法：obj:method(args...)
· 静态方法：Class.method(args...)
· 方法重载：自动匹配（类型优先级同上）。
· 返回值：自动转换为 Lua 类型（见类型映射表）。
· 局限：
  · 方法不存在时抛出 Lua 错误 method not found: xxx。
  · 不支持可变参数（varargs）的精确匹配。

---

1.2 动态代理

java.createProxy(interfaces, handler)

· 参数：
  · interfaces：包含接口名（字符串）的 Lua 表，如 {"java.lang.Runnable"}。
  · handler：Lua 表，键为方法名，值为函数（function(self, ...) end）。
· 返回值：代理对象 userdata。
· 使用场景：用纯 Lua 实现 Java 接口，作为回调传入 Java 方法。
· 限制：
  · 已知严重 bug：跨线程回调（如 Thread:new(proxy):start()）会导致代理的 run 无法执行或崩溃；主线程直接调用 proxy:run() 正常。
  · 对代理对象直接 print 会崩溃，需用 tostring(proxy) 代替。

---

1.3 数组

java.newArray(type, size)

· 参数：type 字符串："int", "double", "boolean", "String"；size 整数。
· 返回值：数组 userdata，支持 [] 读写和 # 取长度。
· 使用场景：需要传递 Java 数组给 Java 方法或操作连续数据。
· 限制：
  · 不支持多维数组、long、float、byte 等基本类型。
  · 越界访问抛出 Lua 错误。

---

1.4 Promise / 协程控制

java.promise()

· 返回值：整数 promise ID。
· 说明：创建一个 pending 状态的 Promise，用于后续异步等待。

java.await(id)

· 参数：id 整数，由 java.promise() 返回。
· 行为：必须在 Lua 协程中调用。挂起当前协程，直到其他线程调用 java.complete(id, ...)。
· 返回值：complete 传入的所有值，作为 await 的多个返回值。
· 使用场景：在主线程中将控制权交给其他任务，等待异步结果。
· 限制：
  · 只能在协程（coroutine.create 环境）中使用，否则报错 attempt to yield from outside a coroutine。
  · 不能真正跨线程：complete 由主线程调用，不能由 Java 新线程通过代理调用（因为代理跨线程 bug）。多线程需用轮询方案（checkPromise，见下）。

java.complete(id, ...)

· 参数：id 整数，可变参数 ... 为要传递的结果。
· 行为：将 Promise 标记为完成，并唤醒在 java.await(id) 处挂起的协程，传入结果。
· 限制：
  · 若在主线程中调用（当前唯一安全方式），必须确保主线程未持锁（doString 已释放锁）。重复 complete 无影响。
  · 跨线程调用需要锁保护，目前未实现（只能用轮询）。

轮询方案（仅限 feature/runasync 分支，main 可能未合并）

· java.runAsync(id, function)：在新 OS 线程中执行 Lua 函数，完成后自动 complete。main 中不存在！
· java.checkPromise(id)：返回 done, result。done 为布尔值，result 为完成时的值。可用于在主线程轮询等待。

---

1.5 类型映射表

Lua 类型 Java 参数类型 Java 返回值类型 → Lua 类型
integer int / Integer Integer → integer
number double / Double Double → number
string String String → string
boolean boolean / Boolean Boolean → boolean
nil null null → nil
userdata 对应 Java 对象 任意对象 → userdata
function LuaFunctionObj（包装） 不支持直接返回 Java 函数

注意：Java long 类型在 Lua 中以 integer 表示，精度可能丢失；float 不支持。

---

2. Java 侧 API

2.1 LuaRuntime 类（com.luajava.LuaRuntime）

线程安全：所有方法已加锁，可在多线程调用。但一个 LuaRuntime 实例在同一时刻只能有一个线程执行 Lua 代码。

方法 说明
LuaRuntime() 构造一个新的 Lua 虚拟机，自动加载 java 模块。
doString(String script) 执行一段 Lua 代码。脚本中的错误会抛出 RuntimeException。
doFile(String path) 执行一个 Lua 脚本文件。
setGlobal(String name, String value) 设置 Lua 全局变量（仅字符串值）。
getGlobal(String name) 获取 Lua 全局变量的字符串值。
callFunction(String name, Object... args) 调用 Lua 全局函数，传入参数，返回第一个返回值。
callFunctionMultiple(String name, Object... args) 调用 Lua 全局函数，返回 Object[] 包含所有返回值。
compile(String luaCode) 编译一段 Lua 代码，返回 LuaFunctionObj 对象。
registerModule(Object module) 注册一个带有 @LuaModule 注解的 Java 对象，将其 @LuaFunction 方法暴露为 Lua 全局函数。
close() 关闭 Lua 虚拟机，释放资源。

限制：

· setGlobal/getGlobal 目前只支持字符串值；可通过 callFunction 传递其他类型。
· compile 返回的函数在 x86_64 Linux 上可能崩溃（SIGSEGV），ARM64 正常。
· registerModule 的方法名称根据注解生成：@LuaModule("prefix") + @LuaFunction("name") → prefix_name。若未指定，直接用方法名。

2.2 LuaFunctionObj 类（com.luajava.LuaFunctionObj）

说明：由 LuaRuntime.compile 返回，代表一个可在 Java 侧多次调用的 Lua 函数。

方法 说明
call(Object... args) 调用函数，返回第一个返回值。
callMultiple(Object... args) 调用函数，返回所有返回值组成的 Object[]。
destroy() 释放函数引用，之后不可再调用。

2.3 注解 @LuaModule 和 @LuaFunction

· @LuaModule("prefix")：标注在 Java 类上，用于注册时的前缀，不可省略。
· @LuaFunction("name")：标注在方法上，name 为 Lua 中暴露的函数名（可省略，默认为方法名）。

示例：

```java
@LuaModule("math")
class MyMath {
    @LuaFunction
    public int add(int a, int b) { return a + b; }
    @LuaFunction("multiply")
    public int mul(int a, int b) { return a * b; }
}
LuaRuntime L = new LuaRuntime();
L.registerModule(new MyMath());
L.doString("print(math_add(3, 5))"); // 8
L.doString("print(math_multiply(6, 7))"); // 42
```

2.4 内部类（用户一般不直接使用）

· LuaInvocationHandler：动态代理的 invocation handler，由 java.createProxy 内部使用。
· LuaJavaCallback：注解绑定时的回调包装，由 registerModule 内部使用。

---

3. 命令行工具 luaj

```bash
luaj                    # 启动 REPL
luaj script.lua         # 执行脚本
luaj -e "code"          # 执行一行代码
luaj -v                 # 显示版本信息
luaj -h                 # 帮助
```

REPL 特性：

· 支持左右方向键、上下历史记录、Ctrl+C 中断当前行、Ctrl+D 退出。
· =expr 快捷打印表达式结果（如 =2+2）。
· 多行输入自动检测（if/for/while/function 等），提示符变为 >>。

---

4. 内部重要函数（供开发者参考）

以下均在 jni/luajava.c 或 lualibjava.c 中实现，不对外暴露。

函数 作用
throwLuaError 将 Lua 错误转换为 Java RuntimeException。
push_java_arg / lua_to_java_object 类型转换核心。
try_find_method / try_method_combinations 方法重载匹配。
promise_registry Promise 链表头指针（需非 static 以便跨文件访问）。
lua_mutex 全局递归锁（?）实际为 pthread_mutex_t，保护 Lua state。

---

5. 已知限制与常见问题

1. 方法重载：复杂重载可能匹配不准，尽量避免同名多参方法。
2. 代理跨线程：createProxy 的对象不能在新线程中调用方法，会死锁或崩溃。
3. compile 在 x86_64：SIGSEGV，待修复。
4. 类型支持不全：long（部分支持）、float、byte、short、char 未完整支持。
5. Promise 不能跨线程 complete（main 分支）：java.complete 只能在主线程调用，否则需要锁保护。
6. JLine 警告：Termux 下因缺少 libutil.so.1 抛出无害警告。
7. 字段不存在时的行为：返回方法查找器而非报错，可能造成困惑。

---

6. 项目文件结构（核心）

目录/文件 内容
Lua5.4.8/lualibjava.c Lua java 库的全部实现（方法调用、字段、数组、代理、Promise）
Lua5.4.8/lualib_async.c 异步 runAsync 相关（仅 feature 分支，main 无）
jni/luajava.c Java 到 Lua 的 JNI 桥接（LuaRuntime 等 native 方法、类型转换）
src/LuaRuntime.java Java 入口类
src/LuaJMain.java 命令行解释器
src/LuaFunctionObj.java 编译后的函数句柄
src/LuaInvocationHandler.java 动态代理回调
src/LuaJavaCallback.java 注解绑定回调
src/LuaFunction.java / LuaModule.java 注解定义
CMakeLists.txt 构建脚本
Makefile 顶层构建（调用 cmake + javac）

日后若有更新，以实际代码为准。