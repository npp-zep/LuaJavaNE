local java = require("java")
local Thread = java.import('java.lang.Thread')

local id = java.promise()

local co = coroutine.create(function()
    print('waiting...')
    local result = java.await(id)
    print('type:', type(result))
    print('result:', result)
end)

coroutine.resume(co)

java.runAsync(id, function()
    return 'async result'
end)

print('main continues...')
Thread.sleep(500)
print('main done')
