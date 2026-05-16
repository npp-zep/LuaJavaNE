java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
    end
end

print("step1 parseInt")
local id = java.promise()
java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
print(await(id))

print("step2 parseDouble")
id = java.promise()
java.runAsync(id, 'java.lang.Double', 'parseDouble', '3.14')
print(await(id))

print("step3 parseBoolean")
id = java.promise()
java.runAsync(id, 'java.lang.Boolean', 'parseBoolean', 'true')
print(await(id))

print("step4 sleep")
id = java.promise()
java.runAsync(id, 'java.lang.Thread', 'sleep', '10')
print(await(id))

print("done")
