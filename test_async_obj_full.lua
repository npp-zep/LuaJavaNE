java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
    end
end

print("=== 异步构造 String ===")
local id = java.promise()
java.runAsync(id, 'java.lang.String', 'new', 'Hello Async')
local objId = await(id)
print("Object ID:", objId, "type:", type(objId))

print("=== 获取对象 userdata ===")
local obj = java.getObject(objId)
print("Object:", obj)
print("type:", type(obj))

if obj then
    print("=== 主线程调用 length ===")
    local len = obj:length()
    print("Length:", len)

    print("=== 异步调实例方法 ===")
    local id2 = java.promise()
    java.runAsyncObj(id2, obj, 'substring', '0', '5')
    local sub = await(id2)
    print("Substring:", sub)
end
