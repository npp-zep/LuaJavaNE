-- 导入
local HttpServer = java.import("com.luajava.compat.LuaHttpServer")
local LuaRuntime = java.import("com.luajava.LuaRuntime")

-- 创建 Lua 运行时
local L = LuaRuntime:new()

-- 创建服务器并设置运行时
local server = HttpServer:new()
server:setLuaRuntime(L)

-- 定义全局函数（必须在调用 route 之前定义）
function hello(req, res)
    res:send("<h1>Hello from Lua!</h1><p>Path: " .. req:getPath() .. "</p>")
end

function json(req, res)
    res:json('{"message": "Hello JSON"}')
end

function echo(req, res)
    local body = req:getBody()
    res:send("You sent: " .. body)
end

function user(req, res)
    local name = req:getQueryParam("name") or "Guest"
    res:send("<h1>Hello, " .. name .. "!</h1>")
end

-- 注册路由
server:route("/", "hello")
server:route("/json", "json")
server:route("/echo", "echo")
server:route("/user", "user")

print("Starting server on http://localhost:8080")
server:run(8080)

print("Press Enter to stop...")
io.read()
server:stop()