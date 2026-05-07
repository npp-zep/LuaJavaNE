local java = require("java")
local Thread = java.import('java.lang.Thread')
local System = java.import('java.lang.System')
local iterations = 5000

print("=== 写性能对比 (5000次) ===")

-- 1. Lua 存 -> C store
local start = os.clock()
for i = 1, iterations do
    java.store("bench_num", i)
end
print("C store write:", string.format("%.6f", os.clock() - start), "s")

-- 2. Lua 存 -> Java 字段（通过代理对象）
-- 先用一个 Java 对象做字段写入
local Point = java.import('java.awt.Point')
local p = Point:new(0, 0)
start = os.clock()
for i = 1, iterations do
    p.x = i
end
print("Java field write:", string.format("%.6f", os.clock() - start), "s")

-- 3. Lua 存 -> Java 方法调用
start = os.clock()
for i = 1, iterations do
    p:setLocation(i, 0)
end
print("Java method write:", string.format("%.6f", os.clock() - start), "s")

print("")
print("=== 读性能对比 (5000次) ===")
local result

-- 1. C store 读
start = os.clock()
for i = 1, iterations do
    result = java.fetch("bench_num")
end
print("C store read:", string.format("%.6f", os.clock() - start), "s")

-- 2. Java 字段读
start = os.clock()
for i = 1, iterations do
    result = p.x
end
print("Java field read:", string.format("%.6f", os.clock() - start), "s")

-- 3. Lua 全局变量读
start = os.clock()
for i = 1, iterations do
    result = _G.benchGlobal
end
print("Lua global read:", string.format("%.6f", os.clock() - start), "s")

print("")
print("=== 总结 ===")
print("C store 适合：大量数值计算中间结果、需要保持精度")
print("Java 字段适合：已有 Java 对象的属性读写")
print("Lua 全局适合：Lua 内部的快速共享")
