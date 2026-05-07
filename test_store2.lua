local java = require("java")

-- 测试1: 存储 long 大数（Lua integer 极限）
java.store("bigNum", 9223372036854775807)
print("bigNum:", java.fetch("bigNum"))

-- 测试2: 存储小数精度
java.store("precise", 1.234567890123456789)
print("precise:", java.fetch("precise"))

-- 测试3: 覆盖存储
java.store("x", 1)
java.store("x", "replaced")
print("x after overwrite:", java.fetch("x"))

-- 测试4: 测试5: 空字符串
java.store("empty", "")
print("empty:", java.fetch("empty"))

-- 测试6: 大量存储
for i = 1, 1000 do
    java.store("key" .. i, i)
end
print("1000 keys stored, fetch key500:", java.fetch("key500"))

-- 测试7: 删除后存储新值
java.store("temp", "old")
java.deleteStore("temp")
java.store("temp", "new")
print("temp after re-store:", java.fetch("temp"))
