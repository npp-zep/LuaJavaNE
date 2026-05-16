java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
    end
end

print("=== parseInt ===")
local id = java.promise()
java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
print(await(id))

print("=== sleep(50ms) ===")
id = java.promise()
java.runAsync(id, 'java.lang.Thread', 'sleep', '50')
print(await(id))

print("=== valueOf(double) ===")
id = java.promise()
java.runAsync(id, 'java.lang.String', 'valueOf', '3.14')
print(await(id))

print("done")
