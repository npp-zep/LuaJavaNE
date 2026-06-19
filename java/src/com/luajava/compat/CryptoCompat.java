package com.luajava.compat;

import java.security.*;
import java.util.Base64;

public class CryptoCompat {
    public static String md5(String input) throws Exception {
        return hash(input, "MD5");
    }
    public static String sha256(String input) throws Exception {
        return hash(input, "SHA-256");
    }
    public static String sha1(String input) throws Exception {
        return hash(input, "SHA-1");
    }
    private static String hash(String input, String algo) throws Exception {
        MessageDigest md = MessageDigest.getInstance(algo);
        byte[] digest = md.digest(input.getBytes());
        StringBuilder sb = new StringBuilder();
        for (byte b : digest) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    public static String base64Encode(String input) {
        return Base64.getEncoder().encodeToString(input.getBytes());
    }
    public static String base64Decode(String input) {
        return new String(Base64.getDecoder().decode(input));
    }
}
