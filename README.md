
![Lua](https://img.shields.io/badge/Lua-5.4.8-blue) ![Java](https://img.shields.io/badge/Java-17%2B-orange) ![License](https://img.shields.io/badge/license-MIT-green)

# LuaJavaNE

Lua 5.4.8 + Java 双向互调引擎。在 Lua 里直接调用任何 Java 类库，在 Java 里执行 Lua 脚本。

## 快速开始

### 下载预编译版本

 ARM64 (Termux / Android / 树莓派) 用户请下载 `luajava-arm64.so` 并重命名为 `luajava.so`。

从 [Releases](https://github.com/npp-zep/LuaJavaNE/releases) 下载 `luajava.jar` 和 `luajava.so`。

```bash
java -Dluajava.library.path=./luajava.so -jar luajava.jar -e "print('Hello')"
java -Dluajava.library.path=./luajava.so -jar luajava.jar
```

本地编译

```bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
mkdir build && cd build && cmake .. && make -j4 && cd ..
javac -d out src/*.java && jar cvf luajava.jar -C out .
./luaj.sh -e "print(java.import('java.lang.System').currentTimeMillis())"
```

需要 JDK 17+、CMake 3.14+、GCC/Clang，以及 pthread。

ARM64 / Android / Termux：Release 提供的 .so 是 x86_64 Linux 的。ARM64 用户需要本地编译，编译步骤同上，CMake 会自动检测架构。

作为库使用

Java 调用 Lua

```java
LuaRuntime L = new LuaRuntime();
L.doString("function add(a, b) return a + b end");
Object result = L.callFunction("add", 3, 5); // 8

// 编译函数反复调用
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
print(s:equals("Hello World")) -- true
```

注解绑定

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
L.doString("print(math_add(3, 5))");      // 8
L.doString("print(math_multiply(6, 7))"); // 42
```

动态代理

```lua
local Runnable = java.import("java.lang.Runnable")
local Thread = java.import("java.lang.Thread")
local handler = {
    run = function(self) print("Hello from Lua thread!") end
}
local proxy = java.createProxy({"java.lang.Runnable"}, handler)
local t = Thread:new(proxy)
t:start()
```

数组

```lua
local arr = java.newArray("int", 3)
arr[0] = 100; arr[1] = 200; arr[2] = 300
print(#arr)        -- 3
print(arr[1])      -- 200
```

API 参考

Java 侧

方法 说明
LuaRuntime() 创建 Lua 虚拟机
doString(script) 执行 Lua 代码
doFile(path) 执行 Lua 文件
setGlobal(k, v) 设置全局字符串变量
getGlobal(k) 获取全局字符串变量
callFunction(name, args...) 调用全局函数，返回第一个值
callFunctionMultiple(name, args...) 调用全局函数，返回所有值
compile(code) 编译代码为 LuaFunctionObj
registerModule(obj) 注册带 @LuaModule 注解的对象
close() 关闭虚拟机

Lua 侧

函数 说明
java.import("类名") 导入 Java 类
类:new(参数...) 调用构造方法
对象:方法(参数...) 调用实例方法
类.静态方法(参数...) 调用静态方法
类.字段 / 对象.字段 读写字段
java.createProxy({接口...}, 表) 用 Lua 表实现 Java 接口
java.newArray("类型", 大小) 创建 Java 数组

类型映射

Lua Java
integer int / Integer
number (float) double / Double
string String
boolean boolean / Boolean
userdata Object
function LuaFunctionObj
array userdata Java 数组

已知问题

方法重载解析不完美

当 Java 方法有多个重载时，引擎按返回类型优先级 String > int > boolean > double > long > void > Object 尝试匹配第一个成功的。在歧义情况下可能不精确（例如 int f(int) 和 double f(double) 同时存在时），表现为报 method not found。

临时方案：避免在 Java 侧设计过于复杂的重载；如需精确匹配，可用 Java 包装方法。

代理对象的 print / __tostring 可能崩溃

对 createProxy 创建的代理对象调用 print() 有时会导致段错误。使用 tostring(proxy) 或直接不打印代理对象可规避。

线程回调不保证即时执行

createProxy 的回调在新线程中执行时，Lua 状态是线程安全的（有锁），但多线程 Lua 代码的调度顺序取决于 JVM 线程调度。

ARM64 用户需自行编译

CI 不提供 ARM64 的 .so，Termux / Android / 树莓派用户需要本地编译。

架构

```
Lua (调用 java.xxx) → lualibjava.c → JNI → Java 类
Java (调用 doString) → jni/luajava.c → Lua C API → Lua 解释器
```

· Lua5.4.8/ — Lua 5.4.8 源码，仅新增 lualibjava.c 和修改 linit.c
· jni/ — JNI 桥接代码（Java ↔ Lua C API）
· src/ — Java 库
· CMakeLists.txt — 编译 Lua 静态库 + JNI 动态库

许可证

MIT

作者

npp-zep

