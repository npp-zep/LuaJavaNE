java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id)
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10)
    end
end

-- 在主线程创建 Java 对象
local String = java.import('java.lang.String')
local s = String:new('Hello World')

-- 异步调实例方法
print("=== length ===")
local id = java.promise()
java.runAsyncObj(id, s, 'length')
print(await(id))

print("=== substring ===")
id = java.promise()
java.runAsyncObj(id, s, 'substring', '0', '5')
print(await(id))

print("done")
