java = require("java")
local Thread = java.import('java.lang.Thread')

local function test(name, className, method, arg)
    local id = java.promise()
    java.runAsync(id, className, method, arg)
    while true do
        local done, result = java.checkPromise(id)
        if done then
            print(name .. ": " .. tostring(result))
            break
        end
        Thread.sleep(10)
    end
end

test("类不存在", "java.lang.NonExistent", "foo", "")
test("方法不存在", "java.lang.String", "nonExistentMethod", "")
test("参数类型不匹配", "java.lang.Integer", "parseInt", "not_a_number")
test("正常调用", "java.lang.Integer", "parseInt", "42")
