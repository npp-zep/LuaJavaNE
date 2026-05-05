-- 测试 1：创建 promise 并主线程 complete
local p = java.promise()
print('1: promise created')

-- 在协程里 await
local co = coroutine.create(function()
    print('2: inside coroutine, about to await')
    local result = java.await(p)
    print('4: resumed, result:', result)
end)

coroutine.resume(co)
print('3: after first resume')

-- 主线程直接 complete
p:complete('hello from main')
print('5: done')
