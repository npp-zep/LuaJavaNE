# Java 端调用 Lua API 参考手册

本文档面向 Java 开发者，介绍如何在 Java 代码中使用 **LuaJavaNE** 提供的 API 来执行 Lua 脚本、调用 Lua 函数、以及将 Java 对象暴露给 Lua 环境。

---

## 1. 快速开始

### 1.1 添加依赖

将以下 JAR 文件放入 classpath：

- `luajava.jar` – Java 端核心库
- `lib/jline.jar` – 可选，用于 REPL 行编辑支持

同时确保本机动态库 `luajava.so`（Linux/Android）或 `luajava.dylib`（macOS）位于系统库路径或通过 `-Dluajava.library.path` 指定。

### 1.2 创建 Lua 虚拟机

```java
import com.luajava.LuaRuntime;

LuaRuntime L = new LuaRuntime();
```

每个 `LuaRuntime` 实例对应一个独立的 Lua 状态机（`lua_State*`）。  
**重要**：使用完毕后必须调用 `L.close()` 释放本机资源，或利用 try-with-resources（实现了 `AutoCloseable`）。

---

## 2. 执行 Lua 代码

### 2.1 执行代码字符串

```java
L.doString("print('Hello from Lua!')");
L.doString("x = 42");
```

### 2.2 执行脚本文件

```java
L.doFile("/path/to/script.lua");
```

### 2.3 编译函数（预编译）

对于需要多次调用的函数，可以预编译为 `LuaFunctionObj` 以提高性能。注意 `compile()` 编译的是一个 **chunk**，若 chunk 本身返回函数，需要先调用一次才能拿到该函数：

```java
LuaFunctionObj chunk = L.compile("return function(a, b) return a + b end");
LuaFunctionObj add = (LuaFunctionObj) chunk.call();   // 执行 chunk，得到内部函数
Object[] results = add.callMultiple(3, 5);            // results[0] == 8
chunk.destroy();
add.destroy(); // 释放本机引用
```

编译失败（`LUA_ERRSYNTAX`）会抛出 `LuaSyntaxError`（`LuaRuntimeError` 的子类），而不是返回 `null`。

### 2.4 LuaRuntime 方法速查

| 方法 | 说明 |
|---|---|
| `void doString(String)` / `void doFile(String)` | 执行脚本；语法错误抛 `LuaSyntaxError`，运行错误抛 `LuaRuntimeError` |
| `LuaFunctionObj compile(String)` | 编译 chunk；返回 `LuaFunctionObj` |
| `Object callFunction(String, Object...)` | 调用全局函数，返回第一个值 |
| `Object[] callFunctionMultiple(String, Object...)` | 调用全局函数，返回全部值 |
| `void setGlobal(String, Object)` | 写入全局变量（见第 4 章） |
| `String getGlobal(String)` | 读取全局变量并转为字符串 |
| `Object getGlobalObject(String)` | 读取全局变量并保留原始类型（含 `LuaTable`/`LuaFunctionObj`） |
| `void registerModule(Object)` | 注册 `@LuaModule`/`@LuaFunction` 注解对象（见第 5 章） |
| `void close()` | 关闭本实例（`AutoCloseable`） |


---

## 3. 调用 Lua 全局函数

### 3.1 调用并获取单个返回值

```java
// Lua 中定义：function add(a, b) return a + b end
Object result = L.callFunction("add", 10, 20); // 返回 Integer 30
```

支持参数类型自动转换：Java 的 `String`、`Integer`、`Double`、`Boolean` 会被映射为 Lua 的对应类型，其他 Java 对象会被包装为 userdata（见第 7 节）。

### 3.2 调用并获取多个返回值

```java
// Lua 中定义：function swap(a, b) return b, a end
Object[] results = L.callFunctionMultiple("swap", "hello", "world");
// results[0] = "world", results[1] = "hello"
```

### 3.3 调用带错误处理的函数

若 Lua 函数抛出错误，`callFunction` 会抛出 `LuaRuntimeError`（其父类即 `RuntimeException`），错误信息来自 Lua 栈。

---

## 4. 操作 Lua 全局变量

### 4.1 设置全局变量（任意 Java 值）

```java
L.setGlobal("name", "LuaJava");   // Lua: name = "LuaJava"
L.setGlobal("count", 42);         // Lua: count = 42（整数）
L.setGlobal("ratio", 0.5);        // Lua: ratio = 0.5（浮点）
L.setGlobal("enabled", true);     // Lua: enabled = true
L.setGlobal("buf", new StringBuilder("hi"));
L.setGlobal("arr", new int[] {1, 2, 3});  // Java.Array，0 基索引
L.setGlobal("nothing", null);     // Lua: nothing = nil
```

字符串、数字、布尔按 Lua 原生类型压栈；其他对象（含数组、Map/List）按 Lua 侧 Java userdata 规则包装。

### 4.2 读取全局变量

```java
String version = L.getGlobal("_VERSION");   // "Lua 5.4.8"
Object count = L.getGlobalObject("count");  // Integer 42
```

`getGlobal` 保留原行为，把值转成字符串；需要原始类型（数字、布尔、Java 对象、LuaTable/LuaFunctionObj）时使用 `getGlobalObject`。Lua 的 `nil` 在两种方法下都返回 `null`。Lua 整数在 32 位范围内返回 `Integer`，超出范围返回 `Long`（完整 64 位值）。

---

## 5. 将 Java 对象注册为 Lua 模块

通过注解 `@LuaModule` 和 `@LuaFunction`，您可以将任意 Java 类的实例方法暴露为 Lua 全局函数。

### 5.1 定义 Java 模块类

```java
import com.luajava.LuaModule;
import com.luajava.LuaFunction;

@LuaModule("math")   // 模块前缀，可选
public class MyMath {
    @LuaFunction
    public int add(int a, int b) {
        return a + b;
    }

    @LuaFunction("mul")   // 自定义函数名
    public int multiply(int a, int b) {
        return a * b;
    }
}
```

### 5.2 注册模块

```java
LuaRuntime L = new LuaRuntime();
L.registerModule(new MyMath());
L.doString("print(math_add(3, 5))");   // 输出 8
L.doString("print(math_mul(6, 7))");   // 输出 42
```

### 5.3 命名规则

- 若未指定 `@LuaModule.value()`，则使用空前缀，函数名即为方法名（或 `@LuaFunction.value()`）。
- 若指定了前缀，则生成全局函数名为 `前缀_函数名`（例如 `math_add`）。

支持的方法参数类型：`int`、`long`、`double`、`float`、`boolean`、`String`，以及这些类型的包装类；也支持 `LuaTable`、`LuaFunctionObj` 与任意 Java 对象/数组（按第 7 节映射）。返回值同样支持上述类型，也可以返回任意 Java 对象（会被包装为 userdata）。

---

## 6. 在 Java 中持有 Lua 函数

### 6.1 获取 Lua 函数引用

`LuaRuntime.compile()` 返回的 `LuaFunctionObj` 代表一个已编译的 Lua 函数（可以是闭包）。

```java
LuaFunctionObj fn = L.compile("return function(x) return x * 2 end");
// 调用该函数获得真正的 double 函数
LuaFunctionObj doubler = (LuaFunctionObj) fn.call();
Object result = doubler.call(21); // 42
fn.destroy();
doubler.destroy();
```

### 6.2 调用 `LuaFunctionObj`

- `call(Object... args)` – 返回第一个返回值
- `callMultiple(Object... args)` – 返回所有返回值数组

### 6.3 资源释放

`LuaFunctionObj` 通过引用计数持有所属的 `lua_State`：即使先调用 `LuaRuntime.close()`，状态也会等到最后一个 `LuaFunctionObj`/`LuaInvocationHandler` 释放后才真正关闭，因此不会出现"状态已释放仍被引用"导致的崩溃。

- 建议显式调用 `destroy()` 释放本机注册表引用（幂等，可重复调用）。
- 未调用时，对象被 GC 回收会经 `java.lang.ref.Cleaner` 兜底释放，但不推荐依赖 GC 时机。

---

## 7. 类型映射规则

### 7.1 Java → Lua

| Java 类型               | Lua 类型          |
|-------------------------|-------------------|
| `null`                  | `nil`             |
| `java.lang.String`      | `string`          |
| `java.lang.Integer` / `int` | `integer`     |
| `java.lang.Long` / `long`   | `integer`     |
| `java.lang.Double` / `double`| `number`      |
| `java.lang.Float` / `float`  | `number`      |
| `java.lang.Boolean` / `boolean`| `boolean`   |
| 其他对象                 | `userdata`（Java 对象代理；`Map`/`List` 额外带 table 语义） |

### 7.2 Lua → Java

| Lua 类型    | Java 返回类型                |
|-------------|-----------------------------|
| `nil`       | `null`                      |
| `boolean`   | `java.lang.Boolean`         |
| `integer`   | `java.lang.Integer`         |
| `number`    | `java.lang.Double`          |
| `string`    | `java.lang.String`          |
| `function`  | `com.luajava.LuaFunctionObj` |
| `table`     | `com.luajava.LuaTable`（惰性活引用，见 7.3） |
| `userdata`  | 保持原 Java 对象引用（如果是通过 Java 传入的） |

当 Lua 函数返回一个 Java 对象时（如通过 Java 方法返回的对象），该对象会被包装为 `userdata`，并在 Java 侧以原类型返回。

> **字符串编码**：Java `String` → Lua 使用标准 UTF-8（emoji 等增补平面字符为 4 字节）；Lua 字符串 → Java 同样按标准 UTF-8 解码。非法 UTF-8 序列或孤立 UTF-16 代理项会替换为 `U+FFFD`。

### 7.3 LuaTable：在 Java 中操作 Lua table

`table` 与 `function` 返回的是**惰性引用**，不复制数据。`LuaTable` 的每次 `get`/`put` 都经 JNI 直接读写原表，因此 Java 侧的修改对 Lua 立即可见，反之亦然。典型来源有三处：

1. Lua 调用 Java 方法时，形参声明为 `LuaTable`；
2. `L.getGlobalObject("t")` 读取一个 table 全局变量；
3. `@LuaFunction` 方法的返回值/参数。

```java
// Lua: t = { name = "lua", n = 5, 10, 20, 30 }
LuaTable t = (LuaTable) L.getGlobalObject("t");
String name = (String) t.get("name");   // "lua"
int n = ((Number) t.get("n")).intValue(); // 5
int len = t.size();                      // 3（# 语义，仅数组部分）
Object[] keys = t.keys();                // 全部键（各自还原为 Java 类型）

t.put("fromJava", "yes");                // 写回，Lua 侧立即可见
t.destroy();                             // 用完释放注册表引用（幂等）
```

| 方法 | 说明 |
|---|---|
| `Object get(Object key)` | 读取键；`nil`/不存在返回 `null`，`table`/`function` 分别返回 `LuaTable`/`LuaFunctionObj` |
| `void put(Object key, Object value)` | 写入/覆盖；Java 对象会按第 7.1 节规则压栈 |
| `int size()` | Lua `#`（`rawlen`），仅对数组部分精确 |
| `Object[] keys()` | 遍历全部键 |
| `void destroy()` | 释放引用；未调用时由 `Cleaner` 兜底 |

> `LuaTable` 与 `LuaFunctionObj` 一样会持有 `lua_State` 的引用计数：即使先 `L.close()`，状态也会等最后一个引用释放后才真正关闭。销毁后再调用 `get`/`put`/`size`/`keys` 会抛出 `IllegalStateException`。

### 7.4 把 Lua 函数作为参数传给 Java

Lua 函数传入 Java 时同样以 `LuaFunctionObj` 接收，可在 Java 侧直接回调：

```java
public static Object applyTwice(LuaFunctionObj f, Object x) {
    Object once = f.call(x);
    return f.call(once);
}
```

```lua
local r = TestBridge.applyTwice(function(v) return v * 2 end, 10)  -- 40
```


---

## 8. 高级交互：动态代理（Lua 实现 Java 接口）

### 8.1 在 Java 中定义接口

```java
public interface Greeting {
    String sayHello(String name);
}
```

### 8.2 在 Lua 中实现该接口

```lua
local Greeting = java.import("com.example.Greeting")
local handler = {
    sayHello = function(self, name)
        return "Hello, " .. name .. " from Lua!"
    end
}
local proxy = java.createProxy({"com.example.Greeting"}, handler)
```

### 8.3 在 Java 中使用代理

```java
// 将 proxy 作为 Object 传入 Java，然后强转为接口
Greeting g = (Greeting) proxy;
System.out.println(g.sayHello("Java")); // Hello, Java from Lua!
```

`java.createProxy` 的 Lua 用法见 `lualibjava.c`，Java 侧只需接收 `Object` 并转型。

---

## 9. 异常处理

LuaJavaNE 提供了一套基于 `RuntimeException` 的结构化异常层次（包 `com.luajava.exception`），
既有 `catch (RuntimeException)` 用法完全向后兼容：

```
java.lang.RuntimeException
 └── LuaJavaException (抽象基类)
     ├── LuaRuntimeError      — Lua 执行期错误（doString/doFile/callFunction 的 pcall 失败）
     │    └── LuaSyntaxError  — Lua 编译期语法错误（compile()，对应 LUA_ERRSYNTAX）
     ├── JavaInvocationError  — Lua 调用已注册 Java 方法失败时对 checked 异常的包装
     └── TypeConversionError  — Lua↔Java 参数类型转换失败
```

- Lua 执行错误会抛出 `LuaRuntimeError`（语法错误为 `LuaSyntaxError`），错误信息包含 Lua 栈回溯。
- Java 方法通过注解暴露给 Lua 时，若抛出异常会被捕获并转换为 Lua 错误，然后在 Java 侧重新抛出：
  方法的 `RuntimeException`/`Error` **原样重抛**，其余 checked 异常统一包装为 `JavaInvocationError`（保留原因）。
- 以上异常均为 `RuntimeException` 的子类，可直接 `catch (RuntimeException)` 统一处理。
- Lua 调用 Java 方法抛出的异常会先转换为 Lua 错误（信息含 `java.lang.异常类: message`）；若该调用发生在 `doString` / `doFile` / `callFunction` 中，Java 侧会以 `LuaRuntimeError` 呈现并保留该信息。

---

## 10. 关闭与资源管理

### 10.1 关闭 LuaRuntime

```java
L.close();  // 释放 lua_State 及所有关联资源
```

若使用 try-with-resources：

```java
try (LuaRuntime L = new LuaRuntime()) {
    L.doString("...");
}
```

### 10.2 关闭 LuaFunctionObj

```java
fn.destroy(); // 释放本机引用（建议显式调用；未调用时 GC 会兜底）
```

### 10.3 全局线程池

LuaJavaNE 内部使用 `LuaAgent` 管理后台线程池（用于异步任务）。该线程池是全局的，会在 JVM 退出时自动终止。如需主动关闭，可调用：

```java
LuaAgent.shutdown();
```

但通常无需手动操作。

Java 侧还可直接使用 `LuaAgent` 的公开入口：`submitTask(AgentTask)` / `submitTaskFuture(...)`（异步任务）、`cancelTask` / `awaitTask`（取消与等待）、`registerObject` / `getObject` / `unregisterObject`（对象池）、`getTaskStats` / `getRegistryStats`（统计）以及 `shutdown` / `shutdownNow`。任务与对象的详细语义见 [AgentV2.md](AgentV2.md) 的“Java 侧扩展”一节。

---

## 11. 运行期加载 JAR（Java 侧）

Lua 侧对应 `java.load(path, {natives=true})`；Java 侧直接使用 `com.luajava.JarLoader`。它是**进程级**的：所有 `LuaRuntime` 共享同一个 `URLClassLoader`，重复加载同一路径会被去重。

```java
import com.luajava.JarLoader;

int added = JarLoader.load("libs/mylib.jar");          // 1=新增，0=已加载过
Class<?> greeter = JarLoader.resolve("demo.Greeter");  // 找不到返回 null（不初始化类）
ClassLoader cl = JarLoader.loader();                   // 供代理/异步等需要 loader 的场景
String[] paths = JarLoader.loadedPaths();              // 已加载路径快照
```

带本地库（`.so` / `.dylib` / `.dll`）的 JAR：

```java
JarLoader.load("libs/native-lib.jar", true);   // true = 提取并绑定本地库
for (String w : JarLoader.drainWarnings()) {
    System.err.println("[warn] " + w);
}
```

| 方法 | 说明 |
|---|---|
| `int load(String path)` / `int load(String path, boolean natives)` | 附加 JAR/ZIP 或 class 目录；`natives=true` 时按当前平台提取本地库并注册给 `findLibrary`；路径非法或提取失败抛 `IOException` |
| `Class<?> resolve(String binaryName)` | 用运行期加载器解析类；失败返回 `null`（等价于 `Class.forName(name, false, loader)`） |
| `ClassLoader loader()` | 运行期加载器，可传给 `Proxy`、`ServiceLoader` 等 |
| `String[] loadedPaths()` | 已加载路径快照（诊断用） |
| `String[] drainWarnings()` | 取出并清空告警：类被 classpath 遮蔽、跨 JAR 重复类、无匹配平台的本地库、重复库名；最多保留 10 条，其余汇总为 `further warnings suppressed` |

注意：父加载器优先，JAR 与 classpath 同名类时使用 classpath 版本；JVM 无法卸载类，也不支持同一类名在两个加载器中共存。

---

## 12. 完整示例

```java
import com.luajava.LuaRuntime;
import com.luajava.LuaFunctionObj;
import com.luajava.LuaModule;
import com.luajava.LuaFunction;

@LuaModule("demo")
class Calculator {
    @LuaFunction
    public int add(int a, int b) { return a + b; }
}

public class Main {
    public static void main(String[] args) throws Exception {
        try (LuaRuntime L = new LuaRuntime()) {
            // 1. 执行脚本
            L.doString("function mul(a, b) return a * b end");

            // 2. 调用 Lua 函数
            Object product = L.callFunction("mul", 6, 7);
            System.out.println("6 * 7 = " + product); // 42

            // 3. 注册 Java 模块
            L.registerModule(new Calculator());
            L.doString("print(demo_add(10, 20))"); // 30

            // 4. 编译并调用 Lua 函数
            LuaFunctionObj fn = L.compile("return function(x) return x + 100 end");
            LuaFunctionObj inc = (LuaFunctionObj) fn.call();
            System.out.println(inc.call(50)); // 150
            fn.destroy();
            inc.destroy();
        }
    }
}
```

---

## 13. 常见问题

**Q：为什么我的 Java 方法没有被注册？**  
A：确保类上有 `@LuaModule` 注解，方法上有 `@LuaFunction`，且方法为 `public`。

**Q：调用 `L.doString` 时抛异常怎么办？**  
A：捕获 `LuaRuntimeError`（语法错误为 `LuaSyntaxError`，二者均为 `RuntimeException` 子类）并查看错误信息，通常是 Lua 语法错误或运行时错误。

**Q：`LuaFunctionObj` 必须调用 `destroy()` 吗？**  
A：建议调用以尽快释放本机资源。得益于引用计数，即使忘记调用或先关闭了 `LuaRuntime`，也不会崩溃：底层状态会在最后一个引用释放（含 GC 时的 `Cleaner` 兜底）后才真正关闭。

**Q：如何传递数组或集合？**  
A：Java 数组作为 `Java.Array` userdata 传入/返回（0 基索引、`#` 取长度）；`java.util.Map`/`List` 会包成带 table 语义的惰性 userdata（`#`、`pairs`/`ipairs`、写回）。Lua 原生 `table` 则以 `LuaTable` 活引用传入 Java（见 7.3）。

**Q：如何在 Java 中拿到 Lua table 或函数？**  
A：用 `L.getGlobalObject(name)` 取回原始类型：table 得到 `LuaTable`，function 得到 `LuaFunctionObj`；也可在 `@LuaFunction` 方法/普通 Java 方法形参中直接声明这两种类型。

**Q：Java 侧如何加载带本地库的 JAR？**  
A：`JarLoader.load(path, true)`；告警通过 `JarLoader.drainWarnings()` 读取，详见第 11 章。

---

## 14. 更多资源

- 项目源码：`java/src/com/luajava/` 包含所有 Java API 类
- 示例脚本：`examples/` 目录下的 Lua 示例
- REPL 启动：`./luaj.sh`（在项目根目录）

---

*本文档基于 LuaJavaNE v2.5.1 及之后的未发布改动编写，如有变动请参考最新源码。*