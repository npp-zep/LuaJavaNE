local java = require("java")

-- === 精度测试 ===
print("=== 精度测试 ===")
local pi = 3.14159265358979323846
java.store("pi", pi)
local fetched = java.fetch("pi")
print("原始:", string.format("%.20f", pi))
print("取回:", string.format("%.20f", fetched))
print("偏差:", math.abs(pi - fetched))

-- long 边界
local bigInt = 9223372036854775807
java.store("bigInt", bigInt)
local fb = java.fetch("bigInt")
print("bigInt 原始:", bigInt)
print("bigInt 取回:", fb)
print("bigInt 偏差:", math.abs(bigInt - fb))

-- === 速度测试 ===
print("\n=== 速度测试 ===")

-- 存储 10000 个数
local start = os.clock()
for i = 1, 10000 do
    java.store("bench" .. i, i)
end
print("store 10000 次:", string.format("%.4f sec", os.clock() - start))

-- 读取 10000 个数
start = os.clock()
local sum = 0
for i = 1, 10000 do
    sum = sum + java.fetch("bench" .. i)
end
print("fetch 10000 次:", string.format("%.4f sec", os.clock() - start))
print("sum:", sum)

-- 10 万次存取（简化版，重复存取同一个 key）
start = os.clock()
for i = 1, 100000 do
    java.store("single", i)
    java.fetch("single")
end
print("10w store+fetch:", string.format("%.3f sec", os.clock() - start))
