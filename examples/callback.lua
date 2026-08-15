-- examples/callback.lua
-- 回调式异步任务示例：使用 java.onComplete(id, callback) 替代轮询
-- 与 examples/async.lua 的 checkPromise 轮询方案对比：
--   轮询：主动反复调用 checkPromise(id) 直到完成
--   回调：注册 callback，任务完成时由后台线程自动触发，无需主动检查
local java = require("java")
local Thread = java.import("java.lang.Thread")

-- 剩余未完成任务数，全部完成后脚本退出
local pending = 0
local function on_done()
    pending = pending - 1
end

-- 等待所有回调完成。
-- 注意：回调在后台工作线程执行，主线程需要让出 lua_mutex 才能让回调跑起来，
-- 因此循环里调用 Thread.sleep 主动释放锁（与 async.lua 轮询循环同理）。
local function wait_all(timeout_ms)
    local waited = 0
    while pending > 0 do
        Thread.sleep(10)
        waited = waited + 10
        if timeout_ms and waited > timeout_ms then
            print(">>> 等待超时，剩余未完成任务: " .. pending)
            return false
        end
    end
    return true
end

print("=== 1. 静态方法回调：java.onComplete(id, function(err, result)) ===")
pending = pending + 1
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
java.onComplete(id, function(err, v)
    if err then
        print("  失败: " .. err)
    else
        print("  parseInt('42') = " .. tostring(v))
    end
    on_done()
end)

print("=== 2. 错误回调：err 携带异常信息 ===")
pending = pending + 1
id = java.promise()
java.runAsync(id, "java.lang.NonExistent", "foo", "")
java.onComplete(id, function(err, v)
    print("  类不存在 -> err = " .. tostring(err))
    on_done()
end)

print("=== 3. 异步构造对象，回调内同步调用实例方法 ===")
pending = pending + 1
id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello Callback")
java.onComplete(id, function(err, oid)
    if err or type(oid) ~= "number" then
        print("  构造失败: " .. tostring(err or oid))
    else
        local obj = java.getObject(oid)
        print("  对象 length = " .. obj:length() .. "，toUpperCase = " .. obj:toUpperCase())
    end
    on_done()
end)

print("=== 4. 链式回调：回调内再提交任务 ===")
pending = pending + 1
id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "21")
java.onComplete(id, function(err, v)
    if err then
        print("  第一步失败: " .. err)
        on_done()
    else
        print("  第一步结果: " .. v)
        pending = pending + 1          -- 链式任务也要计入等待
        local id2 = java.promise()
        java.runAsync(id2, "java.lang.Integer", "parseInt", tostring(v) .. "0")
        java.onComplete(id2, function(e2, v2)
            if e2 then
                print("  第二步失败: " .. e2)
            else
                print("  链式结果: 21 -> " .. v2)
            end
            on_done()
        end)
        on_done()
    end
end)

print("=== 5. 任务先完成、后注册回调（立即派发） ===")
pending = pending + 1
id = java.promise()
java.complete(id, "early-result")
java.onComplete(id, function(err, v)
    print("  立即收到已完成结果: " .. tostring(v))
    on_done()
end)

print("=== 6. 回调与 checkPromise 轮询二选一（此处仅演示轮询并存） ===")
local pid = java.promise()
java.runAsync(pid, "java.lang.Integer", "parseInt", "100")
local done, val = false, nil
while not done do
    Thread.sleep(10)
    done, val = java.checkPromise(pid)
end
print("  轮询结果: " .. tostring(val))

print("=== 等待全部回调完成 ===")
if wait_all(5000) then
    print("全部回调已完成")
else
    print("存在未完成任务")
end
print("回调示例结束")
