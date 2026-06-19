local java = require("java")
local HttpHelper = java.import("com.luajava.HttpHelper")
local System = java.import("java.lang.System")

local PORT = 8080
print("HTTP server started on http://localhost:" .. PORT .. "/")
print("Run: curl http://localhost:" .. PORT .. "/hello")

while true do
    local path = HttpHelper.handleRequest(PORT)
    print("Served:", path)
end
