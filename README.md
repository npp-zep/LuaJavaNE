```markdown
![Lua](https://img.shields.io/badge/Lua-5.4.8-blue) ![Java](https://img.shields.io/badge/Java-17%2B-orange) ![License](https://img.shields.io/badge/license-MIT-green)

# LuaJavaNE

Lua 5.4.8 + Java 双向互调引擎。在 Lua 里直接调用任何 Java 类库，在 Java 里执行 Lua 脚本。

## 快速开始

### 从源码构建

```bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
make
./luaj.sh
```

从 Release 下载

从 Releases 下载 luajava.jar 和 luajava.so（ARM64 用户下载 luajava-arm64.so 并重命名为 luajava.so），放同一目录：

```bash
java -Dluajava.library.path=./luajava.so -jar luajava.jar
```

```lua
> System = java.import('java.lang.System')
> System.currentTimeMillis()
1777819200000
```

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

动态代理

用 Lua 表实现 Java 接口，作为回调传给 Java 方法：

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

API 参考

LuaRuntime（Java 侧入口）

方法 说明
LuaRuntime() 创建 Lua 虚拟机
doString(script) 执行 Lua 代码
doFile(path) 执行 Lua 文件
setGlobal(name, value) 设置全局字符串变量
getGlobal(name) 获取全局字符串变量
callFunction(name, ...) 调用全局函数，返回第一个值
callFunctionMultiple(name, ...) 调用全局函数，返回所有值
compile(code) 编译为 LuaFunctionObj
registerModule(obj) 注册带 @LuaModule 注解的对象
close() 关闭虚拟机

LuaFunctionObj

方法 说明
call(...) 调用，返回第一个值
callMultiple(...) 调用，返回所有值
destroy() 释放引用

Lua 侧 java 库

函数 说明
java.import("类名") 导入 Java 类
类:new(...) 调用构造方法
对象:方法(...) 调用实例方法
类.静态方法(...) 调用静态方法
类.字段 / 对象.字段 读写字段
java.createProxy({接口...}, 表) Lua 表实现 Java 接口
java.newArray("类型", 大小) 创建 Java 数组（int/double/boolean/String）

类型映射

Lua Java Java Lua
integer Integer Integer integer
number Double Double number
string String String string
boolean Boolean Boolean boolean
nil null null nil
userdata Object Object userdata
function LuaFunctionObj — —
array userdata Java 数组 Java 数组 array userdata

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

Java 集合

```lua
local ArrayList = java.import("java.util.ArrayList")
local list = ArrayList:new()
list:add("a")
list:add("b")
print(list:size())     -- 2
print(list:get(0))     -- a
```

常见错误

错误信息 原因
class not found: xxx Java 类名写错或不存在
constructor not found 构造方法的参数类型不匹配
method not found: xxx 方法名写错，或参数类型/数量不对
array index out of bounds: n (size m) 数组越界访问
attempt to index a nil value 对象是 nil，检查赋值是否成功
method threw exception Java 方法内部抛了异常
'end' expected near <eof> 缺了 end，检查 if/for/while 是否闭合

构建

需要 JDK 17+、CMake 3.14+、GCC/Clang、pthread。

```bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
make
./luaj.sh
```

ARM64 (Termux / Android / 树莓派): 编译步骤相同，或从 Releases 下载预编译的 luajava-arm64.so。

已知限制
- `compile()` 方法在 x86_64 Linux 上可能崩溃（SIGSEGV），ARM64 正常。

· 方法重载解析按返回类型优先级（String > int > boolean > double > long > void > Object）尝试，复杂重载可能匹配不精确
· 代理对象直接 print() 可能段错误，用 tostring(proxy) 替代
· 类型 long / float / byte / char 暂不完全支持
· JLine 在 Termux 下会打 WARNING（libutil.so.1 缺失，不影响使用）
· REPL 多行嵌套的 end 检测基于关键字，字符串内的关键字可能误触发

架构

```
Lua (调用 java.xxx)  → lualibjava.c → JNI → Java 类
Java (调用 doString) → jni/luajava.c → Lua C API → Lua 解释器
```

目录 说明
Lua5.4.8/ Lua 5.4.8 源码，新增 lualibjava.c，修改 linit.c
jni/ JNI 桥接代码
src/ Java 库
CMakeLists.txt CMake 构建脚本

贡献

```bash
git clone git@github.com:npp-zep/LuaJavaNE.git
cd LuaJavaNE
git checkout -b feature/my-feature
# 修改代码
make && make test
git commit -m "Description"
git push origin feature/my-feature
# 在 GitHub 上提 Pull Request
```

欢迎任何 Issue 和 PR。

许可证

MIT。本项目包含 Lua 5.4.8 (MIT) 和 JLine 3 (BSD-3)。

作者

npp-zep

```