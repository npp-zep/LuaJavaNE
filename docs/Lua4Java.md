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

对于需要多次调用的函数，可以预编译为 `LuaFunctionObj` 以提高性能：

```java
LuaFunctionObj fn = L.compile("return function(a, b) return a + b end");
// 调用编译后的函数（返回一个 Lua 函数对象）
Object[] results = fn.callMultiple(3, 5); // results[0] == 8
fn.destroy(); // 释放本机引用
```

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

### 4.1 设置全局变量（仅支持字符串值）

```java
L.setGlobal("name", "LuaJava");
// 对应 Lua: name = "LuaJava"
```

> **注意**：`setGlobal` 目前仅支持字符串。若需设置其他类型，请通过执行 `doString` 完成，或使用注解注册的 Java 方法。

### 4.2 读取全局变量（返回字符串）

```java
String version = L.getGlobal("_VERSION");
System.out.println(version); // "Lua 5.4.8"
```

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

支持的方法参数类型：`int`、`long`、`double`、`float`、`boolean`、`String`，以及这些类型的包装类。返回值同样支持上述类型，也可以返回任意 Java 对象（会被包装为 userdata）。

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
- 未调用时，对象被 GC 回收会经 `finalize()` 兜底释放，但不推荐依赖 GC 时机。

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
| 其他对象                 | `userdata`（Java 对象代理） |

### 7.2 Lua → Java

| Lua 类型    | Java 返回类型                |
|-------------|-----------------------------|
| `nil`       | `null`                      |
| `boolean`   | `java.lang.Boolean`         |
| `integer`   | `java.lang.Integer`         |
| `number`    | `java.lang.Double`          |
| `string`    | `java.lang.String`          |
| `function`  | `com.luajava.LuaFunctionObj` |
| `table`     | 暂不支持直接转换（可设计为 Map） |
| `userdata`  | 保持原 Java 对象引用（如果是通过 Java 传入的） |

当 Lua 函数返回一个 Java 对象时（如通过 Java 方法返回的对象），该对象会被包装为 `userdata`，并在 Java 侧以原类型返回。

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

---

## 11. 完整示例

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

## 12. 常见问题

**Q：为什么我的 Java 方法没有被注册？**  
A：确保类上有 `@LuaModule` 注解，方法上有 `@LuaFunction`，且方法为 `public`。

**Q：调用 `L.doString` 时抛异常怎么办？**  
A：捕获 `LuaRuntimeError`（语法错误为 `LuaSyntaxError`，二者均为 `RuntimeException` 子类）并查看错误信息，通常是 Lua 语法错误或运行时错误。

**Q：`LuaFunctionObj` 必须调用 `destroy()` 吗？**  
A：建议调用以尽快释放本机资源。得益于引用计数，即使忘记调用或先关闭了 `LuaRuntime`，也不会崩溃：底层状态会在最后一个引用释放（含 GC 时的 `finalize()` 兜底）后才真正关闭。

**Q：如何传递数组或集合？**  
A：目前支持通过 Java 方法返回数组，或使用 `java.newArray` 在 Lua 中创建 Java 数组，然后传入 Java 方法。

---

## 13. 更多资源

- 项目源码：`java/src/com/luajava/` 包含所有 Java API 类
- 示例脚本：`examples/` 目录下的 Lua 示例
- REPL 启动：`./luaj.sh`（在项目根目录）

---

*本文档基于 LuaJavaNE v2.2.6.1 编写，如有变动请参考最新源码。*