java = require("java")
local Thread = java.import('java.lang.Thread')

local id = java.promise()
java.runAsync(id, "java.lang.System", "getProperty", "java.version")

while true do
    local done, result = java.checkPromise(id)
    if done then
        print("Java version:", result)
        break
    end
    Thread.sleep(10)
end
