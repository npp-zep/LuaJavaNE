java = require("java")
local Thread = java.import('java.lang.Thread')

local id = java.promise()
java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')

while true do
    local done, result = java.checkPromise(id)
    if done then
        print("result:", result)
        break
    end
    Thread.sleep(10)
end
