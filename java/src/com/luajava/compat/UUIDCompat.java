package com.luajava.compat;

import java.util.UUID;

public class UUIDCompat {
    public static String randomUUID() {
        return UUID.randomUUID().toString();
    }
    public static String fromString(String name) {
        return UUID.nameUUIDFromBytes(name.getBytes()).toString();
    }
}
