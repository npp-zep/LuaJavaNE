java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(1)
    end
end

-- Math.pow 可测浮点计算
local ITER = 10000

-- ====== 纯 Lua 本地计算 ======
local start = os.clock()
for i = 1, ITER do
    local x = 0
    for j = 1, 100 do x = x + math.sin(j) * math.cos(j) end
end
print(string.format("Lua本地计算(10000x100): %.4f秒", os.clock() - start))

-- ====== 多线程异步（分8个worker并发算） ======
start = os.clock()
local ids = {}
local batch = math.floor(ITER / 8)
for k = 1, 8 do
    ids[k] = java.promise()
    java.runAsync(ids[k], 'java.lang.Math', 'sin', '1.0')
end
for k = 1, 8 do await(ids[k]) end
print(string.format("8并发Math.sin(1次): %.4f秒", os.clock() - start))

-- ====== 真·多线程并发计算（用 Thread 做累加） ======
-- 每个 worker 在自己线程里循环计算，结果通过 promise 返回
start = os.clock()
ids = {}
for k = 1, 8 do
    ids[k] = java.promise()
    java.runAsync(ids[k], 'com.luajava.AsyncRunner', 'heavyCalc', tostring(batch))
end
for k = 1, 8 do await(ids[k]) end
print(string.format("8并发heavyCalc(各%d次): %.4f秒", batch, os.clock() - start))
