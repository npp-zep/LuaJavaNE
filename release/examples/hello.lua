local java = require("java")

print("=== Lua → Java 基本互调 ===")

-- 导入 Java 类
local System = java.import("java.lang.System")
local String = java.import("java.lang.String")
local Thread = java.import("java.lang.Thread")

-- 调用静态方法
print("Java 版本     :", System.getProperty("java.version"))
print("当前时间(ms)  :", System.currentTimeMillis())

-- 创建对象并调用实例方法
local s = String:new("Hello from LuaJavaNE")
print("字符串        :", s:toString())
print("长度          :", s:length())
print("转大写        :", s:toUpperCase())
print("子串(0, 5)   :", s:substring(0, 5))

-- 算术 + Java 互调
local Integer = java.import("java.lang.Integer")
print("parseInt('42'):", Integer.parseInt("42"))
print("sum 3+5       :", Integer.sum(3, 5))

print("基本互调示例完成")
