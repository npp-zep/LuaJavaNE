local lfs = require("lfs")
print("LFS 版本: " .. lfs._VERSION)
print("当前目录: " .. lfs.currentdir())

-- 列出 /data/data/com.termux/files/home 下的内容
print("\n家目录内容:")
for file in lfs.dir(lfs.currentdir()) do
    if file ~= "." and file ~= ".." then
        local attr = lfs.attributes(file)
        local type_str = attr.mode == "directory" and "[目录]" or "[文件]"
        print(type_str .. " " .. file)
    end
end