package com.luajava.compat;

import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;   // 添加这行

public class ShellCompat {
    // ---- 系统属性 ----
    public static String getProperty(String key) { return System.getProperty(key); }
    public static String getenv(String key) { return System.getenv(key); }
    public static long currentTimeMillis() { return System.currentTimeMillis(); }
    public static int availableProcessors() { return Runtime.getRuntime().availableProcessors(); }
    public static long freeMemory() { return Runtime.getRuntime().freeMemory(); }
    public static long totalMemory() { return Runtime.getRuntime().totalMemory(); }
    public static long maxMemory() { return Runtime.getRuntime().maxMemory(); }

    // ---- 进程执行 ----
    public static String exec(String cmd) throws Exception {
        return exec(cmd, 30000);
    }

    public static String exec(String cmd, long timeoutMs) throws Exception {
        Process p = Runtime.getRuntime().exec(cmd);
        if (!p.waitFor(timeoutMs, TimeUnit.MILLISECONDS)) {
            p.destroyForcibly();
            throw new TimeoutException("Command timed out: " + cmd);
        }
        try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) {
                sb.append(line).append("\n");
            }
            return sb.toString();
        }
    }

    // ---- 进程执行（带错误输出） ----
    public static String[] execFull(String cmd) throws Exception {
        return execFull(cmd, 30000);
    }

    public static String[] execFull(String cmd, long timeoutMs) throws Exception {
        Process p = Runtime.getRuntime().exec(cmd);
        if (!p.waitFor(timeoutMs, TimeUnit.MILLISECONDS)) {
            p.destroyForcibly();
            throw new TimeoutException("Command timed out: " + cmd);
        }
        String stdout;
        String stderr;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) sb.append(line).append("\n");
            stdout = sb.toString();
        }
        try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getErrorStream()))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) sb.append(line).append("\n");
            stderr = sb.toString();
        }
        return new String[]{stdout, stderr, String.valueOf(p.exitValue())};
    }

    // ---- 文件操作 ----
    public static String readFile(String path) throws Exception {
        return new String(Files.readAllBytes(Paths.get(path)), java.nio.charset.StandardCharsets.UTF_8);
    }

    public static void writeFile(String path, String content) throws Exception {
        Files.write(Paths.get(path), content.getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }

    public static void appendFile(String path, String content) throws Exception {
        Files.write(Paths.get(path), content.getBytes(java.nio.charset.StandardCharsets.UTF_8),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
    }

    public static boolean fileExists(String path) { return Files.exists(Paths.get(path)); }
    public static boolean isFile(String path) { return Files.isRegularFile(Paths.get(path)); }
    public static boolean isDirectory(String path) { return Files.isDirectory(Paths.get(path)); }

    public static void fileDelete(String path) throws Exception { Files.delete(Paths.get(path)); }
    public static boolean fileDeleteIfExists(String path) throws Exception {
        return Files.deleteIfExists(Paths.get(path));
    }

    public static void mkdir(String path) throws Exception { Files.createDirectories(Paths.get(path)); }
    public static void mkdirs(String path) throws Exception { Files.createDirectories(Paths.get(path)); }

    public static String[] listFiles(String path) throws Exception {
        File[] files = new File(path).listFiles();
        if (files == null) return new String[0];
        String[] names = new String[files.length];
        for (int i = 0; i < files.length; i++) names[i] = files[i].getName();
        return names;
    }

    public static String[] listFilesFull(String path) throws Exception {
        File[] files = new File(path).listFiles();
        if (files == null) return new String[0];
        String[] names = new String[files.length];
        for (int i = 0; i < files.length; i++) names[i] = files[i].getAbsolutePath();
        return names;
    }

    public static void copyFile(String src, String dst) throws Exception {
        Files.copy(Paths.get(src), Paths.get(dst), StandardCopyOption.REPLACE_EXISTING);
    }

    public static void moveFile(String src, String dst) throws Exception {
        Files.move(Paths.get(src), Paths.get(dst), StandardCopyOption.REPLACE_EXISTING);
    }

    public static long fileSize(String path) throws Exception {
        return Files.size(Paths.get(path));
    }

    public static String fileExtension(String path) {
        int idx = path.lastIndexOf('.');
        return idx > 0 ? path.substring(idx + 1) : "";
    }

    public static String fileName(String path) {
        return new File(path).getName();
    }

    public static String fileDir(String path) {
        return new File(path).getParent();
    }

    // ---- 网络服务 ----
    public static String serverAccept(int port, String response) throws Exception {
        return serverAccept(port, response, "text/html");
    }

    public static String serverAccept(int port, String response, String contentType) throws Exception {
        ServerSocket server = new ServerSocket(port);
        server.setSoTimeout(30000);
        Socket socket = server.accept();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(socket.getInputStream()));
             PrintWriter w = new PrintWriter(socket.getOutputStream())) {
            String line = r.readLine();
            String path = "/";
            if (line != null) {
                String[] p = line.split(" ");
                if (p.length > 1) path = p[1];
            }
            w.println("HTTP/1.1 200 OK");
            w.println("Content-Type: " + contentType);
            w.println("Content-Length: " + response.length());
            w.println();
            w.println(response);
            w.flush();
            server.close();
            return path;
        }
    }

    // ---- 环境信息 ----
    public static String osName() { return System.getProperty("os.name"); }
    public static String osVersion() { return System.getProperty("os.version"); }
    public static String osArch() { return System.getProperty("os.arch"); }
    public static String javaVersion() { return System.getProperty("java.version"); }
    public static String javaVendor() { return System.getProperty("java.vendor"); }
    public static String userHome() { return System.getProperty("user.home"); }
    public static String userDir() { return System.getProperty("user.dir"); }
    public static String tempDir() { return System.getProperty("java.io.tmpdir"); }
}