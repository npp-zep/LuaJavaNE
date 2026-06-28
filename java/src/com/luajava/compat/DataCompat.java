package com.luajava.compat;

import java.security.*;
import java.util.*;
import java.util.regex.*;
import java.net.*;

public class DataCompat {
    // ---- 加密哈希 ----
    public static String md5(String s) throws Exception { return hash(s, "MD5"); }
    public static String sha256(String s) throws Exception { return hash(s, "SHA-256"); }
    private static String hash(String s, String algo) throws Exception {
        MessageDigest md = MessageDigest.getInstance(algo);
        byte[] d = md.digest(s.getBytes());
        StringBuilder sb = new StringBuilder(); for (byte b : d) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    // ---- Base64 ----
    public static String base64Encode(String s) { return Base64.getEncoder().encodeToString(s.getBytes()); }
    public static String base64Decode(String s) { return new String(Base64.getDecoder().decode(s)); }

    // ---- URL 编码 ----
    public static String urlEncode(String s) throws Exception { return URLEncoder.encode(s, "UTF-8"); }
    public static String urlDecode(String s) throws Exception { return URLDecoder.decode(s, "UTF-8"); }

    // ---- 正则 ----
    public static boolean regexMatch(String pattern, String input) { return Pattern.compile(pattern).matcher(input).matches(); }
    public static String regexFind(String pattern, String input) {
        Matcher m = Pattern.compile(pattern).matcher(input); return m.find() ? m.group() : null;
    }
    public static String regexReplace(String pattern, String input, String repl) {
        return Pattern.compile(pattern).matcher(input).replaceAll(repl);
    }

    // ---- UUID ----
    public static String randomUUID() { return UUID.randomUUID().toString(); }
}
