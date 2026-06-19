local crypto = java.import("com.luajava.compat.CryptoCompat")

print("MD5:", crypto.md5("hello"))
print("SHA256:", crypto.sha256("hello"))
print("Base64:", crypto.base64Encode("hello"))
print("Decode:", crypto.base64Decode(crypto.base64Encode("hello")))
