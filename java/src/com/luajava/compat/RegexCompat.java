package com.luajava.compat;

import java.util.regex.*;

public class RegexCompat {
    public static boolean matches(String pattern, String input) {
        return Pattern.compile(pattern).matcher(input).matches();
    }
    public static String find(String pattern, String input) {
        Matcher m = Pattern.compile(pattern).matcher(input);
        return m.find() ? m.group() : null;
    }
    public static String replace(String pattern, String input, String replacement) {
        return Pattern.compile(pattern).matcher(input).replaceAll(replacement);
    }
    public static String[] findAll(String pattern, String input) {
        Matcher m = Pattern.compile(pattern).matcher(input);
        java.util.ArrayList<String> list = new java.util.ArrayList<>();
        while (m.find()) list.add(m.group());
        return list.toArray(new String[0]);
    }
}
