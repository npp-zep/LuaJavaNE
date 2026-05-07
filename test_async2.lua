local java = require("java")

local id = java.promise()

java.runAsync(id, function()
    return 'hello'
end)

local Thread = java.import('java.lang.Thread')
Thread.sleep(500)

local done, result = java.checkPromise(id)
print('done:', done, 'result:', result)
