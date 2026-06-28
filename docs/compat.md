# LuaJavaNE 兼容层 API 参考

当 LuaJavaNE 无法直接调用某些 Java 标准库方法（如无参实例方法、复杂参数类型）时，可使用兼容层提供的静态封装方法。

## ShellCompat — 系统 / 文件 / 进程 / 网络

导入：`local sh = java.import("com.luajava.compat.ShellCompat")`

| 方法 | 参数 | 返回类型 | 说明 |
|------|------|----------|------|
| `getProperty(key)` | String | String | 系统属性（同 `System.getProperty`） |
| `getenv(key)` | String | String | 环境变量 |
| `currentTimeMillis()` | — | long | 毫秒级时间戳 |
| `availableProcessors()` | — | int | CPU 核心数 |
| `exec(command)` | String | String | 执行命令，返回 stdout |
| `readFile(path)` | String | String | 读取文件全部内容 |
| `writeFile(path, content)` | String, String | void | 写入文件 |
| `fileExists(path)` | String | boolean | 文件是否存在 |
| `fileDelete(path)` | String | void | 删除文件 |
| `serverAccept(port, response)` | int, String | String | 启动简易 HTTP 服务器，返回请求路径 |

### 示例

```lua
local sh = java.import("com.luajava.compat.ShellCompat")
print(sh.getProperty("java.version"))   -- 17.0.18
print(sh.availableProcessors())         -- 8
sh.writeFile("test.txt", "Hello")
print(sh.readFile("test.txt"))          -- Hello
print(sh.exec("echo hello"))            -- hello
```

---

DataCompat — 加密 / 编码 / 正则 / UUID

导入：local data = java.import("com.luajava.compat.DataCompat")

方法 参数 返回类型 说明
md5(input) String String MD5 哈希（32位十六进制）
sha256(input) String String SHA-256 哈希（64位十六进制）
base64Encode(input) String String Base64 编码
base64Decode(input) String String Base64 解码
urlEncode(input) String String URL 编码（UTF-8）
urlDecode(input) String String URL 解码（UTF-8）
regexMatch(pattern, input) String, String boolean 正则是否完全匹配
regexFind(pattern, input) String, String String/nil 正则查找第一个匹配
regexReplace(pattern, input, repl) String, String, String String 正则替换全部匹配
randomUUID() — String 随机 UUID

示例

```lua
local data = java.import("com.luajava.compat.DataCompat")
print(data.md5("hello"))                     -- 5d41402abc4b2a76b9719d911017c592
print(data.sha256("hello"))                  -- 2cf24dba...
print(data.base64Encode("hello"))            -- aGVsbG8=
print(data.base64Decode("aGVsbG8="))         -- hello
print(data.urlEncode("hello world"))         -- hello+world
print(data.regexFind("\\d+", "abc123def"))   -- 123
print(data.randomUUID())                     -- xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

---

NetCompat — HTTP 客户端

导入：local net = java.import("com.luajava.compat.NetCompat")

方法 参数 返回类型 说明
httpGet(url) String String HTTP GET，返回响应体
httpPost(url, body) String, String String HTTP POST，返回响应体
httpCode(url) String int HTTP 状态码

示例

```lua
local net = java.import("com.luajava.compat.NetCompat")
print(net.httpCode("https://httpbin.org/get"))           -- 200
local resp = net.httpGet("https://httpbin.org/get")
print(resp:sub(1, 100))                                   -- { "args": {}, ...
```

---

TimeCompat — 日期时间

导入：local time = java.import("com.luajava.compat.TimeCompat")

方法 参数 返回类型 说明
now() — String ISO-8601 UTC 时间戳
nowLocal() — String 本地日期时间
timestamp() — long 毫秒级 Unix 时间戳

示例

```lua
local time = java.import("com.luajava.compat.TimeCompat")
print(time.now())       -- 2026-06-19T03:33:36.492981649Z
print(time.nowLocal())  -- 2026-06-19T03:33:36.492981649
print(time.timestamp()) -- 1781836111036
```

