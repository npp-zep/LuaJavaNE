java = require("java")
local Thread = java.import('java.lang.Thread')

local s = java.import('java.lang.String'):new('a,b,c')
local id = java.promise()
java.runAsyncObj(id, s, 'split', ',')

while true do
    local done, a, b, c = java.checkPromise(id)
    if done then
        print(a, b, c)
        break
    end
    Thread.sleep(10)
end
