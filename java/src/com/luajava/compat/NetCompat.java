package com.luajava.compat;

import java.net.*;
import java.io.*;

public class NetCompat {
    public static String httpGet(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod("GET");
        BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream()));
        StringBuilder sb = new StringBuilder(); String line;
        while ((line = r.readLine()) != null) sb.append(line).append("\n");
        r.close(); c.disconnect(); return sb.toString();
    }

    public static String httpPost(String url, String body) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod("POST"); c.setDoOutput(true);
        c.getOutputStream().write(body.getBytes());
        BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream()));
        StringBuilder sb = new StringBuilder(); String line;
        while ((line = r.readLine()) != null) sb.append(line).append("\n");
        r.close(); c.disconnect(); return sb.toString();
    }

    public static int httpCode(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod("GET"); int code = c.getResponseCode(); c.disconnect(); return code;
    }
}
