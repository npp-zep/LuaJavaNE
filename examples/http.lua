-- examples/http.lua
-- Java HttpClient 在 Lua 中的 HTTP 请求示例（GET / POST / 错误处理）
--
-- 通过 java.import 导入 Java 标准库 java.net.http，在 Lua 中发起 HTTP(S) 请求。
-- 若环境存在 HTTPS_PROXY / HTTP_PROXY 环境变量，示例会自动读取并配置代理
-- （适合在受限网络 / 沙箱中运行；无代理时直接直连）。

local java = require("java")

-- ===== 导入 Java 类 =====
local HttpClient        = java.import("java.net.http.HttpClient")
local HttpRequest       = java.import("java.net.http.HttpRequest")
local BodyPublishers    = java.import("java.net.http.HttpRequest$BodyPublishers")
local BodyHandlers      = java.import("java.net.http.HttpResponse$BodyHandlers")
local URI               = java.import("java.net.URI")
local Duration          = java.import("java.time.Duration")
local InetSocketAddress = java.import("java.net.InetSocketAddress")
local ProxySelector     = java.import("java.net.ProxySelector")

-- ===== 构建 HttpClient（可选代理） =====
local builder = HttpClient:newBuilder()
builder = builder:connectTimeout(Duration:ofSeconds(10))

local proxyUrl = os.getenv("HTTPS_PROXY") or os.getenv("https_proxy")
    or os.getenv("HTTP_PROXY") or os.getenv("http_proxy")
if proxyUrl and proxyUrl ~= "" then
    local host, port = proxyUrl:match("://([^:]+):(%d+)")
    if host and port then
        builder = builder:proxy(ProxySelector:of(InetSocketAddress:new(host, tonumber(port))))
        print("[proxy] " .. proxyUrl)
    end
end

local client = builder:build()

-- ===== 打印响应摘要 =====
local function dump_response(label, resp)
    local status = resp:statusCode()
    local ct = "(none)"
    local hdrs = resp:headers()
    local ctOpt = hdrs:firstValue("Content-Type")
    if ctOpt and ctOpt:isPresent() then ct = ctOpt:get() end
    local body = resp:body() or ""
    print(label .. " -> HTTP " .. tostring(status))
    print("  Content-Type: " .. tostring(ct))
    print("  Body (" .. string.len(body) .. " chars):")
    print("    " .. body)
    print()
end

-- ===== 1. GET 请求 =====
print("=== 1. GET https://httpbin.org/get ===")
do
    local url = "https://httpbin.org/get?name=LuaJavaNE&lang=lua"
    local rb = HttpRequest:newBuilder()
    rb = rb:uri(URI:new(url))
    rb = rb:timeout(Duration:ofSeconds(15))
    rb = rb:header("User-Agent", "LuaJavaNE-http-example")
    rb = rb:GET()
    local req = rb:build()

    local ok, resp = pcall(function() return client:send(req, BodyHandlers:ofString()) end)
    if ok then
        dump_response("GET " .. url, resp)
    else
        print("GET 失败: " .. tostring(resp))
    end
end

-- ===== 2. POST 请求 =====
print("=== 2. POST https://httpbin.org/post ===")
do
    local rb = HttpRequest:newBuilder()
    rb = rb:uri(URI:new("https://httpbin.org/post"))
    rb = rb:timeout(Duration:ofSeconds(15))
    rb = rb:header("Content-Type", "text/plain")
    rb = rb:POST(BodyPublishers:ofString("Hello from LuaJavaNE!"))
    local req = rb:build()

    local ok, resp = pcall(function() return client:send(req, BodyHandlers:ofString()) end)
    if ok then
        dump_response("POST", resp)
    else
        print("POST 失败: " .. tostring(resp))
    end
end

-- ===== 3. 错误处理：不可达地址 =====
print("=== 3. 错误处理：访问不可达地址 ===")
do
    local rb = HttpRequest:newBuilder()
    rb = rb:uri(URI:new("http://127.0.0.1:1/nonexistent"))
    rb = rb:timeout(Duration:ofSeconds(5))
    rb = rb:GET()
    local req = rb:build()

    local ok, err = pcall(function() return client:send(req, BodyHandlers:ofString()) end)
    if ok then
        print("  （未按预期失败）")
    else
        print("  已捕获异常: " .. tostring(err))
    end
end

print("HTTP 示例完成")
