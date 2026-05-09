java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(1)
    end
end

for i = 1, 10 do
    local id = java.promise()
    java.runAsync(id, function() return i end)
    local result = await(id)
    print(i, result)
end
print("done")
