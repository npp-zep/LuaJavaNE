package com.luajava.compat;

public class SystemCompat {
    public static String getProperty(String key) { return System.getProperty(key); }
    public static String getenv(String key) { return System.getenv(key); }
    public static long currentTimeMillis() { return System.currentTimeMillis(); }
    public static long nanoTime() { return System.nanoTime(); }
    public static void gc() { System.gc(); }
    public static int availableProcessors() { return Runtime.getRuntime().availableProcessors(); }
    public static long maxMemory() { return Runtime.getRuntime().maxMemory(); }
}
