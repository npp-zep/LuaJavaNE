java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id, timeout)
    local t = 0
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(1)
        t = t + 1
        if timeout and t > timeout then return "TIMEOUT" end
    end
end

-- ====== 100并发 sleep(10ms) ======
local start = os.clock()
local ids = {}
for i = 1, 100 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '10')
end
for i = 1, 100 do await(ids[i]) end
print(string.format("100并发sleep(10ms): %.4f秒", os.clock() - start))

-- ====== 200并发 sleep(10ms) ======
start = os.clock()
ids = {}
for i = 1, 200 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '10')
end
for i = 1, 200 do await(ids[i]) end
print(string.format("200并发sleep(10ms): %.4f秒", os.clock() - start))

-- ====== 500次快速异步调用 ======
start = os.clock()
for i = 1, 500 do
    local id = java.promise()
    java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
    await(id, 1000)
end
print(string.format("500次快速调用: %.4f秒", os.clock() - start))

-- ====== 50并发heavyCalc(1000次) ======
start = os.clock()
ids = {}
for i = 1, 50 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'com.luajava.AsyncRunner', 'heavyCalc', '1000')
end
for i = 1, 50 do await(ids[i], 5000) end
print(string.format("50并发heavyCalc(1000): %.4f秒", os.clock() - start))
