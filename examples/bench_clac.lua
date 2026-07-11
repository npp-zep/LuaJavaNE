local clac = require("clac")
local math = require("math")
local iterations = 500000

print("=== 计算性能对比(500000次) ===")

local start = os.clock()
for i = 1, iterations do
    local _ = clac.add(i, 0.5)
end
print("clac.add:", string.format("%.6f", os.clock() - start), "s")

start = os.clock()
for i = 1, iterations do
    local _ = i + 0.5
end
print("Lua +:", string.format("%.6f", os.clock() - start), "s")

start = os.clock()
for i = 1, iterations do
    local _ = math.sin(i)
end
print("math.sin:", string.format("%.6f", os.clock() - start), "s")

start = os.clock()
for i = 1, iterations do
    local _ = clac.sin(i)
end
print("clac.sin:", string.format("%.6f", os.clock() - start), "s")
