package com.luajava.compat;

import java.net.*;
import java.io.*;
import java.util.*;
import java.nio.charset.StandardCharsets;
import javax.net.ssl.*;
import java.security.cert.X509Certificate;

public class NetCompat {
    // ---- 信任所有证书（用于测试环境） ----
    private static void trustAllCertificates() {
        try {
            TrustManager[] trustAllCerts = new TrustManager[]{
                new X509TrustManager() {
                    public X509Certificate[] getAcceptedIssuers() { return null; }
                    public void checkClientTrusted(X509Certificate[] certs, String authType) {}
                    public void checkServerTrusted(X509Certificate[] certs, String authType) {}
                }
            };
            SSLContext sc = SSLContext.getInstance("TLS");
            sc.init(null, trustAllCerts, new java.security.SecureRandom());
            HttpsURLConnection.setDefaultSSLSocketFactory(sc.getSocketFactory());
            HttpsURLConnection.setDefaultHostnameVerifier((hostname, session) -> true);
        } catch (Exception e) {
            // 忽略 SSL 初始化错误
        }
    }

    static {
        trustAllCertificates();
    }

    // ---- HTTP GET ----
    public static String httpGet(String url) throws Exception {
        return httpGet(url, 15000);
    }

    public static String httpGet(String url, int timeoutMs) throws Exception {
        try {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("GET");
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("User-Agent", "LuaJavaNE/2.0");
            c.setRequestProperty("Accept", "*/*");
            c.setInstanceFollowRedirects(true);
            return readResponse(c);
        } catch (SocketTimeoutException e) {
            throw new Exception("Connection timeout: " + url);
        } catch (UnknownHostException e) {
            throw new Exception("Unknown host: " + url);
        } catch (Exception e) {
            throw new Exception("HTTP GET failed: " + e.getMessage());
        }
    }

    // ---- HTTP GET with Headers ----
    public static String httpGetWithHeaders(String url, String[] headers) throws Exception {
        return httpGetWithHeaders(url, 15000, headers);
    }

    public static String httpGetWithHeaders(String url, int timeoutMs, String[] headers) throws Exception {
        try {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("GET");
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("User-Agent", "LuaJavaNE/2.0");
            c.setRequestProperty("Accept", "*/*");
            c.setInstanceFollowRedirects(true);
            if (headers != null) {
                for (int i = 0; i < headers.length - 1; i += 2) {
                    c.setRequestProperty(headers[i], headers[i + 1]);
                }
            }
            return readResponse(c);
        } catch (SocketTimeoutException e) {
            throw new Exception("Connection timeout: " + url);
        } catch (UnknownHostException e) {
            throw new Exception("Unknown host: " + url);
        } catch (Exception e) {
            throw new Exception("HTTP GET failed: " + e.getMessage());
        }
    }

    // ---- HTTP POST ----
    public static String httpPost(String url, String body) throws Exception {
        return httpPost(url, body, 15000);
    }

    public static String httpPost(String url, String body, int timeoutMs) throws Exception {
        try {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            c.setRequestProperty("User-Agent", "LuaJavaNE/2.0");
            c.setRequestProperty("Accept", "*/*");
            c.setInstanceFollowRedirects(true);
            try (OutputStream os = c.getOutputStream()) {
                os.write(body.getBytes(StandardCharsets.UTF_8));
            }
            return readResponse(c);
        } catch (SocketTimeoutException e) {
            throw new Exception("Connection timeout: " + url);
        } catch (UnknownHostException e) {
            throw new Exception("Unknown host: " + url);
        } catch (Exception e) {
            throw new Exception("HTTP POST failed: " + e.getMessage());
        }
    }

    // ---- HTTP POST with Headers ----
    public static String httpPostWithHeaders(String url, String body, String[] headers) throws Exception {
        return httpPostWithHeaders(url, body, 15000, headers);
    }

    public static String httpPostWithHeaders(String url, String body, int timeoutMs, String[] headers) throws Exception {
        try {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("User-Agent", "LuaJavaNE/2.0");
            c.setRequestProperty("Accept", "*/*");
            c.setInstanceFollowRedirects(true);
            if (headers != null) {
                for (int i = 0; i < headers.length - 1; i += 2) {
                    c.setRequestProperty(headers[i], headers[i + 1]);
                }
            }
            try (OutputStream os = c.getOutputStream()) {
                os.write(body.getBytes(StandardCharsets.UTF_8));
            }
            return readResponse(c);
        } catch (SocketTimeoutException e) {
            throw new Exception("Connection timeout: " + url);
        } catch (UnknownHostException e) {
            throw new Exception("Unknown host: " + url);
        } catch (Exception e) {
            throw new Exception("HTTP POST failed: " + e.getMessage());
        }
    }

    // ---- HTTP 状态码（使用 GET 并关闭连接） ----
    public static int httpCode(String url) throws Exception {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("GET");
            c.setConnectTimeout(15000);
            c.setReadTimeout(15000);
            c.setRequestProperty("User-Agent", "LuaJavaNE/2.0");
            c.setInstanceFollowRedirects(true);
            // 只获取状态码，不读取响应体
            int code = c.getResponseCode();
            c.disconnect();
            return code;
        } catch (UnknownHostException e) {
            throw new Exception("Unknown host: " + url);
        } catch (SocketTimeoutException e) {
            throw new Exception("Connection timeout: " + url);
        } catch (Exception e) {
            throw new Exception("HTTP status code failed: " + e.getMessage());
        } finally {
            if (c != null) {
                try { c.disconnect(); } catch (Exception e) {}
            }
        }
    }

    // ---- HTTP 完整响应（含状态码） ----
    public static String[] httpGetFull(String url) throws Exception {
        return httpGetFull(url, 15000);
    }

    public static String[] httpGetFull(String url, int timeoutMs) throws Exception {
        try {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod("GET");
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("User-Agent", "LuaJavaNE/2.0");
            c.setRequestProperty("Accept", "*/*");
            c.setInstanceFollowRedirects(true);
            int code = c.getResponseCode();
            String body = readResponse(c);
            c.disconnect();
            return new String[]{String.valueOf(code), body};
        } catch (SocketTimeoutException e) {
            throw new Exception("Connection timeout: " + url);
        } catch (UnknownHostException e) {
            throw new Exception("Unknown host: " + url);
        } catch (Exception e) {
            throw new Exception("HTTP GET failed: " + e.getMessage());
        }
    }

    // ---- 私有辅助方法 ----
    private static String readResponse(HttpURLConnection c) throws IOException {
        int code;
        try {
            code = c.getResponseCode();
        } catch (IOException e) {
            c.disconnect();
            throw e;
        }
        InputStream is = null;
        try {
            is = (code >= 200 && code < 300) ? c.getInputStream() : c.getErrorStream();
        } catch (IOException e) {
            c.disconnect();
            throw e;
        }
        if (is == null) {
            c.disconnect();
            return "";
        }
        try (BufferedReader r = new BufferedReader(new InputStreamReader(is, StandardCharsets.UTF_8))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) {
                sb.append(line).append("\n");
            }
            c.disconnect();
            return sb.toString();
        }
    }
}