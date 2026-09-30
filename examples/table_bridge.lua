local java = require("java")

print("=== table 双向互调测试 ===")

local TestBridge = java.import("testpkg.TestBridge")

-- ===== 方向 A：Lua table -> Java (LuaTable 惰性引用) =====
print("\n--- 方向 A: Lua table -> Java (LuaTable) ---")

local t = {
    name = "world",
    n = 42,
    ok = true,
    x = 5,
    10, 20, 30   -- 数组部分
}

-- Java 端惰性读取（不复制）
TestBridge.analyze(t)          -- 期望 size=3, name=world, n=42, ok=true

-- Java 端遍历键
TestBridge.countKeys(t)

-- Java 端原地写回，Lua 侧立即可见（活引用）
print("方向A写回前 t.x =", t.x)
TestBridge.touch(t)
print("方向A写回后 t.x =", t.x, "  fromJava =", t.fromJava)

-- ===== 方向 B：Java Map/List -> Lua (JavaTable 惰性代理) =====
print("\n--- 方向 B: Java Map -> Lua (JavaTable) ---")

local m = TestBridge.makeMap()
print("map.a =", m.a, " map.b =", m.b, " map.c =", m.c)
print("map.size(#) =", #m)

print("遍历 map:")
for k, v in pairs(m) do
    print("  ", k, "=>", v)
end

-- Java Map 可由 Lua 端惰性写入
m.d = 99
print("map.d =", m.d)

print("\n--- 方向 B: Java List -> Lua (JavaTable 列表) ---")

local l = TestBridge.makeList()
print("list[1] =", l[1], " list[2] =", l[2], " list[3] =", l[3])
print("list.size(#) =", #l)

print("遍历 list:")
for i, v in ipairs(l) do
    print("  ", i, "=>", v)
end

-- Java List 可由 Lua 端惰性写回
l[2] = "changed"
print("list[2] 写回后 =", l[2])

print("\n双向 table 互调测试完成")