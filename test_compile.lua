local f = load("return function(x) return x * 2 end")
local doubler = f()
print(doubler(21))
