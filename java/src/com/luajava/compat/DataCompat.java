package com.luajava.compat;

import java.security.*;
import java.util.*;
import java.util.regex.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import javax.crypto.*;
import javax.crypto.spec.*;
import java.util.Base64;

public class DataCompat {
    // ---- 哈希算法 ----
    public static String md5(String s) throws Exception { return hash(s, "MD5"); }
    public static String sha1(String s) throws Exception { return hash(s, "SHA-1"); }
    public static String sha256(String s) throws Exception { return hash(s, "SHA-256"); }
    public static String sha384(String s) throws Exception { return hash(s, "SHA-384"); }
    public static String sha512(String s) throws Exception { return hash(s, "SHA-512"); }

    private static String hash(String s, String algo) throws Exception {
        MessageDigest md = MessageDigest.getInstance(algo);
        byte[] d = md.digest(s.getBytes(StandardCharsets.UTF_8));
        StringBuilder sb = new StringBuilder();
        for (byte b : d) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    // ---- Hmac 签名 ----
    public static String hmacSha256(String key, String data) throws Exception {
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key.getBytes(StandardCharsets.UTF_8), "HmacSHA256"));
        byte[] result = mac.doFinal(data.getBytes(StandardCharsets.UTF_8));
        StringBuilder sb = new StringBuilder();
        for (byte b : result) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    // ---- Base64 ----
    public static String base64Encode(String s) {
        return Base64.getEncoder().encodeToString(s.getBytes(StandardCharsets.UTF_8));
    }
    public static String base64Decode(String s) {
        return new String(Base64.getDecoder().decode(s), StandardCharsets.UTF_8);
    }
    public static String base64UrlEncode(String s) {
        return Base64.getUrlEncoder().withoutPadding().encodeToString(s.getBytes(StandardCharsets.UTF_8));
    }
    public static String base64UrlDecode(String s) {
        return new String(Base64.getUrlDecoder().decode(s), StandardCharsets.UTF_8);
    }

    // ---- URL 编码 ----
    public static String urlEncode(String s) throws Exception {
        return URLEncoder.encode(s, "UTF-8");
    }
    public static String urlDecode(String s) throws Exception {
        return URLDecoder.decode(s, "UTF-8");
    }

    // ---- 正则 ----
    public static boolean regexMatch(String pattern, String input) {
        return Pattern.compile(pattern).matcher(input).matches();
    }
    public static String regexFind(String pattern, String input) {
        Matcher m = Pattern.compile(pattern).matcher(input);
        return m.find() ? m.group() : null;
    }
    public static String[] regexFindAll(String pattern, String input) {
        Matcher m = Pattern.compile(pattern).matcher(input);
        List<String> results = new ArrayList<>();
        while (m.find()) results.add(m.group());
        return results.toArray(new String[0]);
    }
    public static String regexReplace(String pattern, String input, String repl) {
        return Pattern.compile(pattern).matcher(input).replaceAll(repl);
    }
    public static String regexReplaceFirst(String pattern, String input, String repl) {
        return Pattern.compile(pattern).matcher(input).replaceFirst(repl);
    }

    // ---- UUID ----
    public static String randomUUID() { return UUID.randomUUID().toString(); }
    public static String uuid() { return randomUUID(); }

    // ---- 随机字符串 ----
    public static String randomString(int length) {
        String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        SecureRandom random = new SecureRandom();
        StringBuilder sb = new StringBuilder(length);
        for (int i = 0; i < length; i++) {
            sb.append(chars.charAt(random.nextInt(chars.length())));
        }
        return sb.toString();
    }

    // ---- 字符串工具 ----
    public static boolean isEmpty(String s) { return s == null || s.isEmpty(); }
    public static boolean isNotEmpty(String s) { return s != null && !s.isEmpty(); }
    public static boolean isBlank(String s) { return s == null || s.trim().isEmpty(); }
    public static boolean isNotBlank(String s) { return s != null && !s.trim().isEmpty(); }

    public static String trim(String s) { return s == null ? null : s.trim(); }
    public static String upperCase(String s) { return s == null ? null : s.toUpperCase(); }
    public static String lowerCase(String s) { return s == null ? null : s.toLowerCase(); }
    public static String capitalize(String s) {
        if (s == null || s.isEmpty()) return s;
        return s.substring(0, 1).toUpperCase() + s.substring(1);
    }

    public static String[] split(String s, String regex) {
        return s == null ? new String[0] : s.split(regex);
    }
    public static String join(String[] arr, String sep) {
        return String.join(sep, arr);
    }

    // ---- 类型转换（返回原始类型，失败返回 0/false） ----
    public static int toInt(String s) {
        try { return Integer.parseInt(s); } catch (NumberFormatException e) { return 0; }
    }
    public static long toLong(String s) {
        try { return Long.parseLong(s); } catch (NumberFormatException e) { return 0L; }
    }
    public static double toDouble(String s) {
        try { return Double.parseDouble(s); } catch (NumberFormatException e) { return 0.0; }
    }
    public static boolean toBoolean(String s) {
        return "true".equalsIgnoreCase(s) || "1".equals(s) || "yes".equalsIgnoreCase(s);
    }

    // ---- 类型转换（带默认值） ----
    public static int toIntDefault(String s, int defaultValue) {
        try { return Integer.parseInt(s); } catch (NumberFormatException e) { return defaultValue; }
    }
    public static long toLongDefault(String s, long defaultValue) {
        try { return Long.parseLong(s); } catch (NumberFormatException e) { return defaultValue; }
    }
    public static double toDoubleDefault(String s, double defaultValue) {
        try { return Double.parseDouble(s); } catch (NumberFormatException e) { return defaultValue; }
    }

    // ---- 类型检查 ----
    public static boolean isInt(String s) {
        if (s == null || s.isEmpty()) return false;
        try { Integer.parseInt(s); return true; } catch (NumberFormatException e) { return false; }
    }
    public static boolean isLong(String s) {
        if (s == null || s.isEmpty()) return false;
        try { Long.parseLong(s); return true; } catch (NumberFormatException e) { return false; }
    }
    public static boolean isDouble(String s) {
        if (s == null || s.isEmpty()) return false;
        try { Double.parseDouble(s); return true; } catch (NumberFormatException e) { return false; }
    }
}