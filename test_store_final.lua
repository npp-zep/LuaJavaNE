local java = require("java")

print("=== 1. 基础类型 ===")
java.store("str", "hello")
java.store("num", 3.14159)
java.store("bool", true)
java.store("nilval", nil)
print("str:", java.fetch("str"))
print("num:", java.fetch("num"))
print("bool:", java.fetch("bool"))
print("nil:", java.fetch("nilval"))

print("=== 2. 覆盖更新 ===")
java.store("x", 1)
java.store("x", "updated")
print("x:", java.fetch("x"))

print("=== 3. 删除 ===")
java.store("tmp", "value")
print("before delete:", java.fetch("tmp"))
java.deleteStore("tmp")
print("after delete:", java.fetch("tmp"))

print("=== 4. 大量存储 ===")
for i = 1, 500 do
    java.store("bulk" .. i, i * 2)
end
print("key 250:", java.fetch("bulk250"))
print("key 499:", java.fetch("bulk499"))

print("=== 5. 不存在的 key ===")
print("nonexist:", java.fetch("no_such_key"))

print("=== 6. 特殊字符 key ===")
java.store("key with spaces", "spaces work")
java.store("key\nwith\nnewlines", "newlines work")
print("spaces:", java.fetch("key with spaces"))
print("newlines:", java.fetch("key\nwith\nnewlines"))

print("=== 7. 长字符串 ===")
local longStr = string.rep("a", 10000)
java.store("long", longStr)
local fetched = java.fetch("long")
print("long length:", #fetched)

print("=== 8. 边界数字 ===")
java.store("zero", 0)
java.store("negative", -42.5)
java.store("big", 1e308)
print("zero:", java.fetch("zero"))
print("negative:", java.fetch("negative"))
print("big:", java.fetch("big"))

print("=== All tests passed ===")
