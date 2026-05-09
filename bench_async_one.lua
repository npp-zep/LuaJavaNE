java = require("java")
local Thread = java.import('java.lang.Thread')

print("--- 单次多线程异步 ---")
local id = java.promise()
java.runAsync(id, function()
    local x = 0
    for j = 1, 100 do x = x + j end
    return x
end)
while true do
    local done, result = java.checkPromise(id)
    if done then print("result:", result); break end
    Thread.sleep(1)
end
print("done")
