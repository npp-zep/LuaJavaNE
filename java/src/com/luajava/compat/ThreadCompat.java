package com.luajava.compat;

public class ThreadCompat {
    public static void sleep(long ms) throws Exception { Thread.sleep(ms); }
    public static String currentThreadName() { return Thread.currentThread().getName(); }
}
