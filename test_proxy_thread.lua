local Runnable = java.import('java.lang.Runnable')
local Thread = java.import('java.lang.Thread')

handler = {
    run = function(self)
        print('proxy run called')
    end
}

local proxy = java.createProxy({'java.lang.Runnable'}, handler)
print('proxy created:', tostring(proxy))

local t = Thread:new(proxy)
print('thread created, starting...')
t:start()
print('thread started, waiting...')
Thread.sleep(1000)
print('done')
