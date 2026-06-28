package com.luajava.compat;

import java.io.*;
import java.net.*;
import java.nio.file.*;

public class ShellCompat {
    // ---- 系统属性 ----
    public static String getProperty(String key) { return System.getProperty(key); }
    public static String getenv(String key) { return System.getenv(key); }
    public static long currentTimeMillis() { return System.currentTimeMillis(); }
    public static int availableProcessors() { return Runtime.getRuntime().availableProcessors(); }

    // ---- 进程 ----
    public static String exec(String cmd) throws Exception {
        Process p = Runtime.getRuntime().exec(cmd);
        BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
        StringBuilder sb = new StringBuilder(); String line;
        while ((line = r.readLine()) != null) sb.append(line).append("\n");
        r.close(); p.waitFor(); return sb.toString();
    }

    // ---- 文件读写 ----
    public static String readFile(String path) throws Exception {
        return new String(Files.readAllBytes(Paths.get(path)));
    }
    public static void writeFile(String path, String content) throws Exception {
        Files.write(Paths.get(path), content.getBytes());
    }
    public static boolean fileExists(String path) { return Files.exists(Paths.get(path)); }
    public static void fileDelete(String path) throws Exception { Files.delete(Paths.get(path)); }

    // ---- IO/网络 ----
    public static String serverAccept(int port, String response) throws Exception {
        ServerSocket server = new ServerSocket(port);
        Socket socket = server.accept();
        BufferedReader r = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        PrintWriter w = new PrintWriter(socket.getOutputStream());
        String line = r.readLine(), path = "/";
        if (line != null) { String[] p = line.split(" "); if (p.length > 1) path = p[1]; }
        w.println("HTTP/1.1 200 OK\nContent-Type: text/html\nContent-Length: " + response.length() + "\n\n" + response);
        w.flush(); r.close(); w.close(); socket.close(); server.close();
        return path;
    }
}
