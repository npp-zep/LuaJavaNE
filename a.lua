-- 用 Lua 写 Java 程序，不需要安装 JDK
local System = java.import("java.lang.System")
local Files = java.import("java.nio.file.Files")
local Paths = java.import("java.nio.file.Paths")

Files:writeString(Paths:get("test.txt"), "Hello from Lua")
print("Java version:", System:getProperty("java.version"))
