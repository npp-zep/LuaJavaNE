package com.luajava.compat;

import java.net.URLEncoder;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;

public class URLCompat {
    public static String encode(String input) throws Exception {
        return URLEncoder.encode(input, StandardCharsets.UTF_8);
    }
    public static String decode(String input) throws Exception {
        return URLDecoder.decode(input, StandardCharsets.UTF_8);
    }
}
