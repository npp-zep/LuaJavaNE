local java = require "java"

local Data = java.import("com.luajava.compat.DataCompat")
local Net = java.import("com.luajava.compat.NetCompat")
local Shell = java.import("com.luajava.compat.ShellCompat")
local Time = java.import("com.luajava.compat.TimeCompat")

local function println(s)
    io.write(tostring(s) or "nil")
    io.write("\n")
    io.flush()
end

println("=== DataCompat Test ===")

-- 哈希
println(Data.md5("hello"))
println(Data.sha256("hello"))
println(Data.sha512("hello"))

-- Base64
println(Data.base64Encode("hello"))

-- 随机字符串
println(Data.randomString(16))

-- 字符串工具
println(Data.capitalize("hello"))

-- 正则
println(Data.regexMatch("^h.*$", "hello"))
println(Data.regexFind("l+", "hello"))
println(Data.regexReplace("l", "hello", "x"))

-- 类型转换
println("toInt('123') = " .. tostring(Data.toInt("123")))
println("toInt('abc') = " .. tostring(Data.toInt("abc")))
println("toIntDefault('123', 999) = " .. tostring(Data.toIntDefault("123", 999)))
println("toIntDefault('abc', 999) = " .. tostring(Data.toIntDefault("abc", 999)))
println("isInt('123') = " .. tostring(Data.isInt("123")))
println("isInt('abc') = " .. tostring(Data.isInt("abc")))
println("isInt('') = " .. tostring(Data.isInt("")))

println("")

println("=== NetCompat Test ===")

-- HTTP GET
local ok, result = pcall(Net.httpGet, "https://httpbin.org/get")
if ok then
    println("HTTP GET success!")
    local preview = string.sub(result, 1, 150) .. "..."
    println(preview)
else
    println("HTTP GET failed: " .. tostring(result))
end

-- 获取状态码
local ok2, code = pcall(Net.httpCode, "https://httpbin.org/status/200")
if ok2 then
    println("HTTP status code: " .. tostring(code))
else
    println("HTTP status code failed: " .. tostring(code))
end

println("")

println("=== TimeCompat Test ===")
println("now: " .. Time.now())
println("nowLocal: " .. Time.nowLocal())
println("timestamp: " .. tostring(Time.timestamp()))
println("year: " .. tostring(Time.year()))
println("month: " .. tostring(Time.month()))
println("day: " .. tostring(Time.day()))

println("")

println("=== ShellCompat Test ===")
println("os.name: " .. Shell.osName())
println("user.home: " .. Shell.userHome())
println("user.dir: " .. Shell.userDir())
println("availableProcessors: " .. tostring(Shell.availableProcessors()))

println("")
println("=== All tests passed ===")