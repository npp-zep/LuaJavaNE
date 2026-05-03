
LuaJavaNE

Lua 5.4.8 + Java 双向互调引擎。让 Lua 脚本能直接调用任意 Java 类，Java 也能无缝执行 Lua 代码。

作者： npp-zep
版本： 1.0
基于： Lua 5.4.8

---

快速开始

启动 REPL

```bash
./luaj.sh
```

```
luaj - LuaJavaNE interpreter v1.0
Lua 5.4.8 with Java interop
Build: 2026-05-03 22:49:44
JDK: 17.0.18 (Termux)
Author: npp-zep
Type \q to quit, \h for help.
> = java.import('java.lang.System').currentTimeMillis()
1777819422945
```

执行脚本

```bash
./luaj.sh script.lua
./luaj.sh -e "print('Hello')"
```

在 Java 项目中嵌入

```java
LuaRuntime L = new LuaRuntime();
L.doString("print('Hello from Java')");
L.callFunction("myFunc", arg1, arg2);
L.close();
```

---

Java → Lua

基础 API

方法 说明
L.doString(script) 执行 Lua 代码
L.doFile(path) 执行 Lua 文件
L.setGlobal(name, value) 设置全局变量
L.getGlobal(name) 获取全局变量（返回 String）
L.callFunction(name, args...) 调用全局函数，返回第一个返回值
L.callFunctionMultiple(name, args...) 调用全局函数，返回所有返回值（Object[]）
L.compile(luaCode) 编译代码为 LuaFunctionObj 对象
L.registerModule(obj) 注册带 @LuaModule 注解的对象

LuaFunctionObj

```java
LuaFunctionObj fn = L.compile("return function(x) return x * 2 end");
LuaFunctionObj doubler = (LuaFunctionObj) fn.call();
doubler.call(21);  // → 42
doubler.callMultiple(args...);  // → Object[] 多返回值
doubler.destroy();
```

类型映射（Java→Lua）

Java 类型 转为 Lua 类型
String string
Integer integer
Double number
Boolean boolean
null nil
LuaFunctionObj function

类型映射（Lua→Java）

Lua 类型 转为 Java 类型
string String
integer Integer
number Double
boolean Boolean
nil null
function LuaFunctionObj

---

Lua → Java

Lua 侧通过内置 java 库访问 Java。

导入类

```lua
local String = java.import('java.lang.String')
local Integer = java.import('java.lang.Integer')
local System = java.import('java.lang.System')
```

创建对象

```lua
local s = String:new('Hello World')
local point = java.import('java.awt.Point'):new(10, 20)
```

调用实例方法

```lua
s:length()           -- → 11
s:substring(0, 5)    -- → "Hello"
s:equals('test')     -- → false
s:toUpperCase()      -- → "HELLO WORLD"
```

调用静态方法

```lua
Integer.parseInt('42')       -- → 42
Integer.toString(255)        -- → "255"
System.currentTimeMillis()   -- → 1777819244661
```

访问字段

```lua
-- 读取
Integer.MAX_VALUE      -- → 2147483647
point.x                -- → 10

-- 写入
point.x = 100
point.y = 200
```

数组操作

```lua
local arr = java.newArray('int', 5)
arr[0] = 42
arr[1] = 100
print(#arr)            -- → 5
print(arr[0])          -- → 42

-- 支持的数组类型: "int", "double", "boolean", "String"
```

动态代理（Lua 表实现 Java 接口）

```lua
local Runnable = java.import('java.lang.Runnable')
local Thread = java.import('java.lang.Thread')

local handler = {
    run = function(self)
        print('Hello from Lua proxy thread!')
    end
}

local proxy = java.createProxy({'java.lang.Runnable'}, handler)
local thread = Thread:new(proxy)
thread:start()
```

java.createProxy(interfaceNames, table) 接收接口名数组和 Lua 表，返回一个 Java 代理对象。表中以方法名为键的函数会被调用。

方法重载

自动根据参数类型匹配最合适的重载方法。支持 int、double、String、boolean、Object 的参数组合。

---

注解绑定

用注解将 Java 方法自动暴露为 Lua 全局函数。

```java
@LuaModule("mymod")
class MyModule {
    @LuaFunction
    public int add(int a, int b) { return a + b; }

    @LuaFunction("greet")
    public String hello(String name) { return "Hello, " + name; }
}

LuaRuntime L = new LuaRuntime();
L.registerModule(new MyModule());
L.doString("print(mymod_add(3, 5))");      // → 8
L.doString("print(mymod_greet('World'))"); // → "Hello, World"
```

· @LuaModule("prefix") — 模块名前缀，生成 prefix_methodName 格式的全局函数
· @LuaFunction("name") — 自定义函数名，省略则用 Java 方法名
· 支持 int、String、boolean、double 参数和返回值，自动装箱拆箱

---

线程安全

所有 native 调用都由 pthread 互斥锁保护。多个线程可以安全地共享同一个 LuaRuntime 实例。代理回调（InvocationHandler.invoke）也在锁内执行。

---

库加载策略

LuaRuntime 按以下顺序查找 luajava.so：

1. 系统属性 luajava.library.path
2. 环境变量 LUAJAVA_LIBRARY_PATH
3. java.library.path 中的 libluajava.so
4. 当前目录 ./build/luajava.so、./luajava.so、./libluajava.so

启动时可通过参数指定：

```bash
java -Dluajava.library.path=/path/to/luajava.so ...
```

---

CLI 用法

```bash
luaj                    # 启动 REPL
luaj script.lua         # 执行脚本
luaj -e "code"          # 执行代码
luaj -v                 # 打印版本
luaj -h                 # 打印帮助
```

REPL 特殊命令：

· \q / \quit — 退出
· \h / \help — 帮助
· = expr — 计算并打印表达式结果
· 自动检测多行块（function/if/for/while）

---

构建

```bash
mkdir build && cd build
cmake ..
make -j4
cd ..
javac -d out src/*.java
jar cvf luajava.jar -C out .
```

需要：CMake 3.14+、JDK 8+、GCC/Clang。

---

兼容性

平台 状态
Linux (x86_64) ✅
Linux (AArch64) ✅ (Termux)
macOS ✅ (LUA_USE_MACOSX)
Windows ✅ (LUA_USE_WINDOWS)

---

项目结构

```
LuaJavaNE/
├── Lua5.4.8/              # Lua 5.4.8 源码 + lualibjava.c
├── jni/                   # JNI 桥接 C 代码
├── src/                   # Java 源码
│   ├── LuaRuntime.java    # 主 API
│   ├── LuaFunctionObj.java # 编译后的函数对象
│   ├── LuaInvocationHandler.java # 动态代理 InvocationHandler
│   ├── LuaJavaCallback.java # 注解绑定桥接
│   ├── LuaJMain.java      # CLI 入口
│   ├── LuaFunction.java   # @LuaFunction 注解
│   ├── LuaModule.java     # @LuaModule 注解
│   └── TestLuaJava.java   # 测试套件
├── build/                 # CMake 构建产物
├── CMakeLists.txt
├── luaj.sh                # 启动脚本
└── luajava.jar            # 编译好的 Java 库
```

