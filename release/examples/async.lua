local java = require("java")
local Thread = java.import("java.lang.Thread")

local function await(id, timeout)
    local t = 0
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
        t = t + 10
        if timeout and t > timeout then return "TIMEOUT" end
    end
end

print("=== 异步静态方法 ===")
local id = java.promise()
java.runAsync(id, "java.lang.Integer", "parseInt", "42")
local result = await(id, 2000)
print("parseInt('42'): " .. tostring(result))

print("=== 异步构造对象 ===")
id = java.promise()
java.runAsync(id, "java.lang.String", "new", "Hello Async")
local oid = await(id, 2000)
if oid and type(oid) == "number" then
    local obj = java.getObject(oid)
    print("对象 length: " .. obj:length())

    -- 异步实例方法（当前仅稳定支持无参方法）
    print("=== 异步实例方法 ===")
    id = java.promise()
    java.runAsyncObj(id, obj, "length")
    local len = await(id, 2000)
    print("length: " .. tostring(len))

    -- 同步调用兼容性更好
    print("=== 同步 substring ===")
    print("substring(0,5): " .. obj:substring(0, 5))
else
    print("构造对象失败: " .. tostring(oid))
end

print("异步示例完成")
