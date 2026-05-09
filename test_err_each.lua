java = require("java")
local Thread = java.import('java.lang.Thread')

local function test(name, className, method, arg)
    local id = java.promise()
    java.runAsync(id, className, method, arg)
    while true do
        local done, result = java.checkPromise(id)
        if done then
            print(name .. ": done=" .. tostring(done) .. " result=" .. tostring(result))
            break
        end
        Thread.sleep(10)
    end
end

test("1 类不存在", "java.lang.NonExistent", "foo", "")
print("-- between --")
test("2 正常", "java.lang.Integer", "parseInt", "42")
print("-- done --")
