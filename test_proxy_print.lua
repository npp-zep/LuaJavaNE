local Runnable = java.import('java.lang.Runnable')
local handler = { run = function(self) end }
local proxy = java.createProxy({'java.lang.Runnable'}, handler)
local s = tostring(proxy)
print('type=' .. type(proxy) .. ', tostring=' .. s)
