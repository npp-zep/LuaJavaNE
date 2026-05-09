java = require("java")
local Thread = java.import('java.lang.Thread')

print("--- test1 ---")
local id = java.promise()
java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
while true do
    local done, result = java.checkPromise(id)
    if done then print(result); break end
    Thread.sleep(10)
end

print("--- test2 ---")
id = java.promise()
java.runAsync(id, 'java.lang.Integer', 'parseInt', '99')
while true do
    local done, result = java.checkPromise(id)
    if done then print(result); break end
    Thread.sleep(10)
end
print("done")
