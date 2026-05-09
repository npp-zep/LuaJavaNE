java = require("java")

print("=== 1. 字符串 ===")
do local id = java.promise(); java.runAsync(id, function() return "hello" end); print(java.select(id)) end

print("=== 2. 整数 ===")
do local id = java.promise(); java.runAsync(id, function() return 42 end); print(java.select(id)) end

print("=== 3. 浮点 ===")
do local id = java.promise(); java.runAsync(id, function() return 3.14 end); print(java.select(id)) end

print("=== 4. 布尔 ===")
do local id = java.promise(); java.runAsync(id, function() return true end); print(java.select(id)) end

print("done")
