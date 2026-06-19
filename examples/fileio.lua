local java = require("java")
local file = java.import("com.luajava.compat.FileCompat")
local sys = java.import("com.luajava.compat.SystemCompat")

print("=== File IO ===")
file.writeFile("test_compat.txt", "Hello from LuaJavaNE compat layer!")
print("Exists:", file.exists("test_compat.txt"))
print("Content:", file.readFile("test_compat.txt"))
print("Size:", file.fileSize("test_compat.txt"), "bytes")

print("")
print("=== System ===")
print("Processors:", sys.availableProcessors())
print("Max Memory:", math.floor(sys.maxMemory() / 1024 / 1024) .. " MB")

file.delete("test_compat.txt")
