-- examples/utils.lua
local utils = require("utils")

-- ========== 高精度时间 ==========
print("=== 时间函数 ===")
local t1 = utils.ns_time()
print("ns_time:", t1)

local t2 = utils.monotonic_time()
print("monotonic_time:", t2)

local ts = utils.timestamp()
print("timestamp:", ts)

local ts_ms = utils.timestamp_ms()
print("timestamp_ms:", ts_ms)

-- ========== 高精度休眠 ==========
print("\n=== 休眠测试 ===")
local start = utils.ns_time()

print("sleep 500ms...")
utils.sleep_ms(500)

print("sleep 100ms...")
utils.sleep(0.1)

print("sleep 50ms...")
utils.sleep_ms(50)

local elapsed = utils.ns_time() - start
print(string.format("Total elapsed: %.3f seconds", elapsed))

-- ========== 计时器 ==========
print("\n=== 计时器测试 ===")
local timer = utils.timer()

-- 模拟一些工作
utils.sleep_ms(100)
local e1 = timer:elapsed()
print("elapsed:", e1)

utils.sleep_ms(50)
local l1 = timer:lap()
print("lap:", l1)

utils.sleep_ms(30)
local l2 = timer:lap()
print("lap:", l2)

timer:reset()
utils.sleep_ms(20)
local e2 = timer:elapsed()
print("after reset:", e2)

-- ========== 性能基准 ==========
print("\n=== 性能基准: 测量 utils.sleep 精度 ===")
local function benchmark_sleep(count, duration_ms)
    local t = utils.timer()
    for i = 1, count do
        utils.sleep_ms(duration_ms)
    end
    local total = t:elapsed()
    local expected = count * duration_ms / 1000.0
    local error_pct = (total - expected) / expected * 100
    return total, error_pct
end

local total, err = benchmark_sleep(10, 10)
print(string.format("10次*10ms: 实际=%.3fs, 期望=0.100s, 误差=%.1f%%", total, err))