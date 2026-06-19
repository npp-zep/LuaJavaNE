local java = require("java")
local http = java.import("com.luajava.compat.HttpClientCompat")

print("=== HTTP GET ===")
local resp = http.get("https://httpbin.org/get")
print(resp:sub(1, 200))

print("=== Response Code ===")
print("httpbin.org:", http.responseCode("https://httpbin.org"))
