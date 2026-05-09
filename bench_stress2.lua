java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id, timeout)
    local t = 0
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(1); t = t + 1
        if timeout and t > timeout then return "TIMEOUT" end
    end
end

-- ====== 20并发heavyCalc(1000) ======
local start = os.clock()
local ids = {}
for i = 1, 20 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'com.luajava.AsyncRunner', 'heavyCalc', '1000')
end
local ok, total = 0, 0
for i = 1, 20 do
    local r = await(ids[i], 5000)
    if r ~= "TIMEOUT" then ok = ok + 1 end
    total = total + 1
end
print(string.format("20并发heavyCalc(1000): %.4f秒 (%d/%d)", os.clock() - start, ok, total))

-- ====== 300次快速调用 ======
start = os.clock()
for i = 1, 300 do
    local id = java.promise()
    java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')
    await(id, 1000)
end
print(string.format("300次快速调用: %.4f秒", os.clock() - start))

-- ====== 100并发Thread.sleep(5ms) ======
start = os.clock()
ids = {}
for i = 1, 100 do
    ids[i] = java.promise()
    java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '5')
end
for i = 1, 100 do await(ids[i]) end
print(string.format("100并发sleep(5ms): %.4f秒", os.clock() - start))
