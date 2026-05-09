java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
    end
end

print("=== getProperty ===")
local id = java.promise()
java.runAsync(id, "java.lang.System", "getProperty", "java.version")
print(await(id))

print("=== parseInt ===")
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
print(await(id))

print("=== Thread.sleep 100ms ===")
local id = java.promise()
java.runAsync(id, "java.lang.Thread", "sleep", "100")
print(await(id))

print("=== done ===")
