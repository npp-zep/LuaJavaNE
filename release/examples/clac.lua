local clac = require("clac")

print("=== 基本函数 ===")
print("pi:", clac.pi())
print("sin(pi/2):", clac.sin(clac.pi() / 2))
print("sqrt(144):", clac.sqrt(144))
print("erf(1):", clac.erf(1))
print("tgamma(5):", clac.tgamma(5))

print("=== 批量运算 (ClacArray) ===")
local a = clac.array(5)
local b = clac.array(5)
for i = 1, 5 do
    a[i] = i * 10
    b[i] = i
end
local c = clac.batch_add(a, b)
for i = 1, 5 do
    print(string.format("  a[%d] + b[%d] = %.1f", i, i, c[i]))
end

local sines = clac.batch_sin(a)
print("sin(a[1]):", sines[1])

print("Clac 示例完成")
