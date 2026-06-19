package com.luajava.compat;

import java.nio.file.*;
import java.io.*;

public class FileCompat {
    public static String readFile(String path) throws Exception {
        return new String(Files.readAllBytes(Paths.get(path)));
    }
    public static void writeFile(String path, String content) throws Exception {
        Files.write(Paths.get(path), content.getBytes());
    }
    public static boolean exists(String path) { return Files.exists(Paths.get(path)); }
    public static boolean isDir(String path) { return Files.isDirectory(Paths.get(path)); }
    public static String[] listDir(String path) throws Exception {
        return Files.list(Paths.get(path)).map(p -> p.getFileName().toString())
            .toArray(String[]::new);
    }
    public static void delete(String path) throws Exception { Files.delete(Paths.get(path)); }
    public static long fileSize(String path) throws Exception { return Files.size(Paths.get(path)); }
}
