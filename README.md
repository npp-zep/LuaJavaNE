![Lua](https://img.shields.io/badge/Lua-5.4.8-blue) ![Java](https://img.shields.io/badge/Java-17%2B-orange) ![License](https://img.shields.io/badge/license-MIT-green)

# LuaJavaNE

Lua 5.4.8 + Java 双向互调引擎。在 Lua 里直接调用任何 Java 类库，在 Java 里执行 Lua 脚本。

## 5 分钟快速体验

```bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
mkdir build && cd build && cmake .. && make -j4 && cd ..
javac -cp jline.jar -d out src/*.java && jar cvf luajava.jar -C out .
./luaj.sh
```

```lua
> System = java.import('java.lang.System')
> System.currentTimeMillis()
1777819200000
```

或者从 Releases 下载预编译版本（x86_64 和 ARM64）。

作为库使用

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

动态代理（Lua 实现 Java 接口）

```lua
local Runnable = java.import("java.lang.Runnable")
local Thread = java.import("java.lang.Thread")
local handler = { run = function(self) print("Hello from Lua!") end }
local proxy = java.createProxy({"java.lang.Runnable"}, handler)
Thread:new(proxy):start()
```

数组

```lua
local arr = java.newArray("int", 3)
arr[0] = 100; arr[1] = 200; arr[2] = 300
print(#arr)      -- 3
print(arr[1])    -- 200
```

API 参考

LuaRuntime（Java 侧入口）

方法 说明
LuaRuntime() 创建 Lua 虚拟机
doString(String script) 执行 Lua 代码
doFile(String path) 执行 Lua 文件
setGlobal(String name, String value) 设置全局字符串变量
getGlobal(String name) 获取全局字符串变量
callFunction(String name, Object... args) 调用全局函数，返回第一个值
callFunctionMultiple(String name, Object... args) 调用全局函数，返回所有值
compile(String code) 编译为 LuaFunctionObj，可反复调用
registerModule(Object module) 注册带 @LuaModule 注解的对象
close() 关闭虚拟机

LuaFunctionObj（编译后的函数）

方法 说明
call(Object... args) 调用函数，返回第一个值
callMultiple(Object... args) 调用函数，返回所有值
destroy() 释放引用

Lua 侧 java 库

函数 说明
java.import("类名") 导入 Java 类
类:new(参数...) 调用构造方法
对象:方法(参数...) 调用实例方法
类.静态方法(参数...) 调用静态方法
类.字段 / 对象.字段 读写字段
java.createProxy({接口...}, 表) 用 Lua 表实现 Java 接口
java.newArray("类型", 大小) 创建 Java 数组（支持 int/double/boolean/String）

类型映射

输入（Lua） 输出（Java） 输入（Java） 输出（Lua）
integer Integer Integer integer
number Double Double number
string String String string
boolean Boolean Boolean boolean
nil null null nil
userdata Object Object userdata
function LuaFunctionObj — —
array userdata Java 数组 Java 数组 array userdata

命令行工具

```bash
luaj                    # 启动 REPL
luaj script.lua         # 执行脚本
luaj -e "print(1+1)"    # 执行代码
luaj -v                 # 版本信息
luaj -h                 # 帮助
```

REPL 特性：方向键、历史记录、Ctrl+R 搜索、Ctrl+C 取消当前行、Ctrl+D 退出、=expr 快捷打印。

常见场景

HTTP 请求

```lua
local URL = java.import("java.net.URL")
local url = URL:new("https://api.github.com")
local conn = url:openConnection()
conn:setRequestMethod("GET")
local stream = conn:getInputStream()
local Scanner = java.import("java.util.Scanner")
local s = Scanner:new(stream):useDelimiter("\\A")
print(s:next())
```

文件读写

```lua
local Files = java.import("java.nio.file.Files")
local Paths = java.import("java.nio.file.Paths")
local content = Files:readString(Paths:get("test.txt"))
print(content)
```

系统信息

```lua
local System = java.import("java.lang.System")
print("OS:", System:getProperty("os.name"))
print("Java:", System:getProperty("java.version"))
print("Time:", System:currentTimeMillis())
```

调用 Java 集合

```lua
local ArrayList = java.import("java.util.ArrayList")
local list = ArrayList:new()
list:add("a")
list:add("b")
print(list:size())     -- 2
print(list:get(0))     -- a
```

构建

需要 JDK 17+、CMake 3.14+、GCC/Clang、pthread。

```bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
mkdir build && cd build && cmake .. && make -j4 && cd ..
javac -cp jline.jar -d out src/*.java && jar cvf luajava.jar -C out .
./luaj.sh
```

ARM64 (Termux / Android / 树莓派) 用户：编译步骤相同。预编译的 luajava-arm64.so 可在 Releases 下载。

已知限制

· 方法重载解析按返回类型优先级尝试（String > int > boolean > double > long > void > Object），复杂重载可能匹配不精确
· 代理对象直接 print() 可能段错误，用 tostring(proxy) 替代
· 代理回调在其他线程执行时，Lua 状态有锁保护但调度顺序取决于 JVM
· 类型 long / float / byte / char 以及多维数组暂不支持
· JLine 在 Termux 下会打无影响的 WARNING（缺少 libutil.so.1）
· REPL 多行嵌套的 end 检测基于关键字，字符串内的关键字会误触发

架构

```
Lua (调用 java.xxx)  → lualibjava.c → JNI → Java 类
Java (调用 doString) → jni/luajava.c → Lua C API → Lua 解释器
```

核心文件：

目录 说明
Lua5.4.8/ Lua 5.4.8 源码，新增 lualibjava.c，修改 linit.c
jni/ JNI 桥接代码（luajava.c、luajava.h）
src/ Java 库（LuaRuntime、注解、REPL 等）
CMakeLists.txt CMake 构建脚本

贡献

欢迎提 Issue 和 PR。MIT 协议，随意使用。

作者

npp-zep
