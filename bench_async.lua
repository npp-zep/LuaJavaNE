java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(1)
    end
end

-- ====== 100次串行 ======
local start = os.clock()
for i = 1, 100 do
    local id = java.promise()
    java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
    await(id)
end
print(string.format("异步调Java(100次): %.4f秒", os.clock() - start))

-- ====== 10并发 sleep(100ms) ======
local ids = {}
start = os.clock()
for i = 1, 10 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '100')
end
for i = 1, 10 do await(ids[i]) end
print(string.format("10并发sleep(100ms): %.4f秒", os.clock() - start))

-- ====== 50并发 sleep(10ms) ======
ids = {}
start = os.clock()
for i = 1, 50 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '10')
end
for i = 1, 50 do await(ids[i]) end
print(string.format("50并发sleep(10ms): %.4f秒", os.clock() - start))
