package com.luajava.compat;

public class StringCompat {
    public static byte[] getBytes(String s) { return s.getBytes(); }
    public static String fromBytes(byte[] b) { return new String(b); }
    public static int length(String s) { return s.length(); }
    public static String substring(String s, int begin, int end) { return s.substring(begin, end); }
}
