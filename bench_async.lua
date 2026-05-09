java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(1)
    end
end

local ITER = 100

-- ====== 单线程直接执行 ======
local start = os.clock()
for i = 1, ITER do
    local x = 0
    for j = 1, 100 do x = x + j end
end
print("单线程直接执行:", string.format("%.4f秒", os.clock() - start))

-- ====== 多线程异步执行（轻量计算） ======
start = os.clock()
for i = 1, ITER do
    local id = java.promise()
    java.runAsync(id, function()
        local x = 0
        for j = 1, 100 do x = x + j end
        return x
    end)
    await(id)
end
print("多线程异步(轻量):", string.format("%.4f秒", os.clock() - start))

-- ====== 多线程异步执行（含 Java 调用） ======
start = os.clock()
for i = 1, ITER do
    local id = java.promise()
    java.runAsync(id, function()
        local Thread = java.import('java.lang.Thread')
        return "ok"
    end)
    await(id)
end
print("多线程异步(含Java):", string.format("%.4f秒", os.clock() - start))
