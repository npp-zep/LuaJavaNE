-- proxy.lua — java.createProxy 动态代理示例
--
-- java.createProxy({接口名列表}, handler表) 创建 Java 动态代理对象，
-- 接口方法调用会派发到 handler 表中对应的 Lua 函数（参数按顺序传入，
-- 返回值自动转换回 Java 类型）。

local java = require("java")
local Thread = java.import("java.lang.Thread")

-- 简单的断言辅助
local pass, fail = 0, 0
local function t(desc, fn)
    local ok, err = pcall(fn)
    if ok then
        pass = pass + 1
        print(string.format("  [OK]   %s", desc))
    else
        fail = fail + 1
        print(string.format("  [FAIL] %s -> %s", desc, tostring(err)))
    end
end

print("=== 1. IntBinaryOperator（int 参数 + int 返回值） ===")
t("applyAsInt(10, 20) == 30", function()
    local proxy = java.createProxy({"java.util.function.IntBinaryOperator"},
        { applyAsInt = function(self, a, b) return a + b end })
    assert(proxy:applyAsInt(10, 20) == 30)
end)

print("=== 2. Predicate（String 参数 + boolean 返回值） ===")
t("test('hello')==true / test('world')==false", function()
    local proxy = java.createProxy({"java.util.function.Predicate"},
        { test = function(self, s) return s == "hello" end })
    assert(proxy:test("hello") == true)
    assert(proxy:test("world") == false)
end)

print("=== 3. Function（String 返回值，经 toString 校验） ===")
t("apply('world') 拼接字符串", function()
    local proxy = java.createProxy({"java.util.function.Function"},
        { apply = function(self, s) return "hello " .. s end })
    local r = proxy:apply("world")
    assert(r:toString() == "hello world")
end)

print("=== 4. Consumer（void 方法 + 闭包捕获） ===")
t("accept('xyz') 写入 Lua 局部变量", function()
    local received
    local proxy = java.createProxy({"java.util.function.Consumer"},
        { accept = function(self, s) received = s end })
    proxy:accept("xyz")
    assert(received == "xyz")
end)

print("=== 5. Object.toString 由 handler 实现 ===")
t("toString() 返回自定义字符串", function()
    local proxy = java.createProxy({"java.lang.Runnable"},
        { toString = function(self) return "my-proxy" end })
    assert(proxy:toString() == "my-proxy")
end)

print("=== 6. Runnable 在后台线程运行（java.yield 等待） ===")
do
    local ran = false
    local proxy = java.createProxy({"java.lang.Runnable"},
        { run = function(self) ran = true end })
    local th = Thread:new(proxy)
    th:start()
    -- 等待期间用 java.yield 释放 lua_mutex，子线程的回调才能执行
    local waited = 0
    while not ran do
        java.yield(10)
        waited = waited + 10
        if waited > 3000 then break end
    end
    t("run() 在子线程中被调用", function() assert(ran == true) end)
end

print(string.format("\n结果: %d 通过, %d 失败", pass, fail))
if fail > 0 then os.exit(1) else os.exit(0) end
