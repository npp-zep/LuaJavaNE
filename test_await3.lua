-- 测试：多个 promise 顺序 await
local function delayReturn(val)
    local p = java.promise()
    -- 模拟异步：下一行立即 complete（主线程场景）
    p:complete(val)
    return p
end

local co = coroutine.create(function()
    local a = java.await(delayReturn("first"))
    print('got a:', a)
    local b = java.await(delayReturn("second"))
    print('got b:', b)
    print('sum:', a .. b)
end)

coroutine.resume(co)
