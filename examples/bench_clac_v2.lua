local clac = require("clac")
local iterations = 100000

print("=== 基本运算 (100000次) ===")
local start = os.clock()
for i = 1, iterations do local _ = clac.add(i, 0.5) end
print("clac.add:", string.format("%.6fs", os.clock() - start))

start = os.clock()
for i = 1, iterations do local _ = i + 0.5 end
print("Lua +:", string.format("%.6fs", os.clock() - start))

-- ClacArray 批量测试
print("\n=== ClacArray 批量加法 (10000元素 × 100次) ===")
local a = clac.array(10000)
local b = clac.array(10000)
for i = 1, 10000 do a[i] = math.random(); b[i] = math.random() end

start = os.clock()
for _ = 1, 100 do local c = clac.batch_add(a, b) end
print("clac.batch_add:", string.format("%.6fs", os.clock() - start))

start = os.clock()
for _ = 1, 100 do
    local c = {}
    for i = 1, 10000 do c[i] = a[i] + b[i] end
end
print("Lua table:", string.format("%.6fs", os.clock() - start))

print("\n=== 新增函数 ===")
print("erf(1):", clac.erf(1))
print("tgamma(5):", clac.tgamma(5))
print("log2(8):", clac.log2(8))
