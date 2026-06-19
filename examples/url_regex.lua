local url = java.import("com.luajava.compat.URLCompat")
local regex = java.import("com.luajava.compat.RegexCompat")

print("URL Encode:", url.encode("hello world"))
print("URL Decode:", url.decode("hello%20world"))

print("Regex match:", regex.matches("\\d+", "12345"))
print("Regex find:", regex.find("\\d+", "abc123def"))
print("Regex replace:", regex.replace("\\d+", "a1b2c3", "X"))
