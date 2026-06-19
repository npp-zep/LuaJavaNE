local uuid = java.import("com.luajava.compat.UUIDCompat")
local proc = java.import("com.luajava.compat.ProcessCompat")

print("UUID:", uuid.randomUUID())

local result = proc.exec("echo hello from shell")
print("Exec:", result:gsub("\n", ""))
