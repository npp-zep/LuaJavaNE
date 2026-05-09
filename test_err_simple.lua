java = require("java")
local Thread = java.import('java.lang.Thread')

-- 先测正常
local id = java.promise()
java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
while true do
    local done, result = java.checkPromise(id)
    if done then print("正常:", result); break end
    Thread.sleep(10)
end

-- 再测错误
id = java.promise()
java.runAsync(id, 'java.lang.NonExistent', 'foo', '')
while true do
    local done, result = java.checkPromise(id)
    if done then print("错误:", result); break end
    Thread.sleep(10)
end

print("done")
