package com.luajava.compat;

import java.time.*;
import java.time.format.*;

public class TimeCompat {
    public static String now() { return Instant.now().toString(); }
    public static String nowLocal() { return LocalDateTime.now().format(DateTimeFormatter.ISO_LOCAL_DATE_TIME); }
    public static long timestamp() { return System.currentTimeMillis(); }
}
