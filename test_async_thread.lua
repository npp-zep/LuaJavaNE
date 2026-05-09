java = require("java")
local Thread = java.import('java.lang.Thread')

print("--- async in worker thread ---")
local id = java.promise()
java.runAsync(id, function() return "hello from thread" end)
while true do
    local done, result = java.checkPromise(id)
    if done then print(result); break end
    Thread.sleep(10)
end
print("done")
