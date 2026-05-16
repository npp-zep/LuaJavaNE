java = require("java")
local Thread = java.import('java.lang.Thread')

local function await(id, timeout)
    local t = 0
    while true do
        local done, result = java.checkPromise(id)
        if done then return result end
        Thread.sleep(10); t = t + 10
        if timeout and t > timeout then return "TIMEOUT" end
    end
end

print("=== 基础类型 ===")
local function test(name, cls, method, arg, expected)
    local id = java.promise()
    java.runAsync(id, cls, method, arg)
    local r = await(id, 2000)
    local status = (tostring(r) == tostring(expected)) and "OK" or ("FAIL(expected=" .. tostring(expected) .. ")")
    print(name .. ": " .. status .. " -> " .. tostring(r))
end

test("String",       "java.lang.String", "valueOf", "123",      "123")
test("int",          "java.lang.Integer", "parseInt", "42",      42)
test("double",       "java.lang.Double", "parseDouble", "3.14",  3.14)
test("boolean",      "java.lang.Boolean", "parseBoolean", "true", true)
test("sleep",        "java.lang.Thread", "sleep", "1",           nil)

print("")
print("=== 错误处理 ===")
test("类不存在",     "java.lang.NoSuch", "foo", "",   "java.lang.ClassNotFoundException: java.lang.NoSuch")
test("方法不存在",   "java.lang.Math", "noSuchMethod", "", "no matching method: noSuchMethod")
test("参数类型错误", "java.lang.Integer", "parseInt", "abc", "java.lang.NumberFormatException: For input string: \"abc\"")

print("")
print("=== 并发 ===")
local start = os.clock()
local ids = {}
for i = 1, 50 do
    ids[i] = java.promise()
    java.runAsync(ids[i], "java.lang.Thread", "sleep", "10")
end
for i = 1, 50 do await(ids[i], 5000) end
print("50并发sleep(10ms): " .. string.format("%.3fs", os.clock() - start))

start = os.clock()
for i = 1, 100 do
    local id = java.promise()
    java.runAsync(id, "java.lang.Integer", "parseInt", "42")
    await(id, 1000)
end
print("100次快速调用: " .. string.format("%.3fs", os.clock() - start))

print("")
print("=== done ===")
