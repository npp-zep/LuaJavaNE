local java = require("java")
local iterations = 500000

print("=== 计算方式对比(500000次) ===")

-- 1. 直接从 C store 取值和回写
java.store("x", 1000.0)
local start = os.clock()
for i = 1, iterations do
    local x = java.fetch("x")
    x = x + 0.5
    java.store("x", x)
end
print("fetch→calc→store:", string.format("%.6f", os.clock() - start), "s")

-- 2. 取值一次，计算，写回一次
java.store("x", 1000.0)
start = os.clock()
local x = java.fetch("x")
for i = 1, iterations do
    x = x + 0.5
end
java.store("x", x)
print("fetch once→calc→store once:", string.format("%.6f", os.clock() - start), "s")

-- 3. 纯 Lua 本地计算
local x = 1000.0
start = os.clock()
for i = 1, iterations do
    x = x + 0.5
end
print("Lua local:", string.format("%.6f", os.clock() - start), "s")

-- 4. 从 Java 字段取值和回写
local Point = java.import('java.awt.Point')
local p = Point:new(1000, 0)
start = os.clock()
for i = 1, iterations do
    local x = p.x
    x = x + 0.5
    p.x = x
end
print("Java field fetch→calc→store:", string.format("%.6f", os.clock() - start), "s")
