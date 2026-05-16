java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
    end
end

print("=== 异步构造 String ===")
local id = java.promise()
java.runAsync(id, 'java.lang.String', 'new', 'Hello Async')
local objStr = await(id)
print("Object:", objStr)
print("type:", type(objStr))
