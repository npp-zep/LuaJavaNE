local clac = require("clac")

print("=== 基本运算 ===")
print("add:", clac.add(3, 5))
print("div:", clac.div(10, 3))

print("=== 取整 ===")
print("floor:", clac.floor(3.7))
print("ceil:", clac.ceil(3.2))
print("round:", clac.round(3.5))

print("=== 幂与对数 ===")
print("pow:", clac.pow(2, 10))
print("sqrt:", clac.sqrt(144))
print("log:", clac.log(100, 10))

print("=== 三角函数 ===")
print("sin(pi/2):", clac.sin(clac.pi() / 2))
print("cos(0):", clac.cos(0))

print("=== 随机 ===")
print("random:", clac.random())
print("random(100):", clac.random(100))
print("random(50, 60):", clac.random(50, 60))

print("=== 角度 ===")
print("rad(180):", clac.rad(180))
print("deg(pi):", clac.deg(clac.pi()))
