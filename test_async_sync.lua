java = require("java")

print("=== 同步执行（Agent 模式） ===")
local id1 = java.promise()
java.runAsync(id1, function() return "hello" end)
local done1, result1 = java.checkPromise(id1)
print(result1)

local id2 = java.promise()
java.runAsync(id2, function() return 42 end)
local done2, result2 = java.checkPromise(id2)
print(result2)

local id3 = java.promise()
java.runAsync(id3, function() return true end)
local done3, result3 = java.checkPromise(id3)
print(result3)

print("done")
