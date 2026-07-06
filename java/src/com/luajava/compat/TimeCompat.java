package com.luajava.compat;

import java.util.concurrent.TimeoutException;
import java.time.*;
import java.time.format.*;
import java.util.*;

public class TimeCompat {
    // ---- 当前时间 ----
    public static String now() { return Instant.now().toString(); }
    public static String nowLocal() {
        return LocalDateTime.now().format(DateTimeFormatter.ISO_LOCAL_DATE_TIME);
    }
    public static long timestamp() { return System.currentTimeMillis(); }
    public static long timestampNano() { return System.nanoTime(); }
    public static long timestampSeconds() { return System.currentTimeMillis() / 1000; }

    // ---- 格式化 ----
    public static String format(String pattern) {
        return LocalDateTime.now().format(DateTimeFormatter.ofPattern(pattern));
    }

    public static String formatDate(String pattern, long timestampMs) {
        return LocalDateTime.ofInstant(Instant.ofEpochMilli(timestampMs), ZoneId.systemDefault())
                .format(DateTimeFormatter.ofPattern(pattern));
    }

    // ---- 解析 ----
    public static long parse(String dateStr, String pattern) {
        LocalDateTime dt = LocalDateTime.parse(dateStr, DateTimeFormatter.ofPattern(pattern));
        return dt.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli();
    }

    public static long parseISO(String dateStr) {
        return Instant.parse(dateStr).toEpochMilli();
    }

    // ---- 时间计算 ----
    public static long addSeconds(long ts, long seconds) { return ts + seconds * 1000; }
    public static long addMinutes(long ts, long minutes) { return ts + minutes * 60 * 1000; }
    public static long addHours(long ts, long hours) { return ts + hours * 60 * 60 * 1000; }
    public static long addDays(long ts, long days) { return ts + days * 24 * 60 * 60 * 1000; }

    public static long diffSeconds(long ts1, long ts2) { return (ts2 - ts1) / 1000; }
    public static long diffMinutes(long ts1, long ts2) { return (ts2 - ts1) / (60 * 1000); }
    public static long diffHours(long ts1, long ts2) { return (ts2 - ts1) / (60 * 60 * 1000); }
    public static long diffDays(long ts1, long ts2) { return (ts2 - ts1) / (24 * 60 * 60 * 1000); }

    // ---- 日期组件 ----
    public static int year() { return LocalDate.now().getYear(); }
    public static int month() { return LocalDate.now().getMonthValue(); }
    public static int day() { return LocalDate.now().getDayOfMonth(); }
    public static int hour() { return LocalTime.now().getHour(); }
    public static int minute() { return LocalTime.now().getMinute(); }
    public static int second() { return LocalTime.now().getSecond(); }

    public static int dayOfWeek() { return LocalDate.now().getDayOfWeek().getValue(); }
    public static int dayOfYear() { return LocalDate.now().getDayOfYear(); }

    // ---- 睡眠 ----
    public static void sleep(long ms) throws InterruptedException {
        Thread.sleep(ms);
    }
}