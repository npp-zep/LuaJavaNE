package com.luajava;

import java.io.*;
import java.util.*;
import org.jline.reader.*;

public class LuaJMain {
    public static void main(String[] args) {
        if (args.length == 0) { 
            try { 
                repl(); 
            } catch (IOException e) { 
                System.err.println(e.getMessage()); 
            } 
            return; 
        }
        String arg = args[0];
        if (arg.equals("-e") && args.length >= 2) { 
            execString(args[1]); 
        } else if (arg.equals("-v") || arg.equals("--version")) { 
            version(); 
        } else if (arg.equals("-h") || arg.equals("--help")) { 
            help(); 
        } else { 
            execFile(args[0], java.util.Arrays.copyOfRange(args, 1, args.length)); 
        }
    }

    static void version() {
        String buildTime = getBuildTime();
        String javaVer = System.getProperty("java.version");
        String javaVendor = System.getProperty("java.vendor");
        String osName = System.getProperty("os.name");
        String osArch = System.getProperty("os.arch");

        System.out.println("LuaJavaNE (Lua 5.4.8, " + buildTime + ")");
        System.out.println("[" + javaVendor + " JDK " + javaVer + " on " + osName + " " + osArch + "]");
        System.out.println("Built with: " + getCCVersion());
        System.out.println("Type \"help\", \"copyright\", \"credits\" or \"license\" for more information.");
    }

    static String getBuildTime() {
        try {
            java.util.jar.JarFile jar = new java.util.jar.JarFile(new File("luajava.jar"));
            java.util.jar.Manifest mf = jar.getManifest();
            if (mf != null) {
                String val = mf.getMainAttributes().getValue("Build-Time");
                if (val != null) {
                    jar.close();
                    return val;
                }
            }
            jar.close();
        } catch (Exception ignored) {
        }
        return new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss").format(new java.util.Date());
    }

    static String getCCVersion() {
        try {
            Process p = Runtime.getRuntime().exec(new String[]{"cc", "--version"});
            java.io.BufferedReader r = new java.io.BufferedReader(
                new java.io.InputStreamReader(p.getInputStream()));
            String line = r.readLine();
            r.close();
            p.waitFor();
            return line != null ? line : "Unknown";
        } catch (Exception e) {
            return "Unknown";
        }
    }

    static void help() {
        System.out.println("luaj - LuaJavaNE interpreter (Lua 5.4.8 + Java)");
        System.out.println("Usage: luaj [-e code] [-v] [-h] [script.lua] [args...]");
    }

    static void execString(String code) {
        try (LuaRuntime rt = new LuaRuntime()) {
            rt.doString("java = require 'java'");
            rt.doString(code);
        } catch (Exception e) {
            System.err.println(e.getMessage());
        }
    }

    static void execFile(String path, String[] args) {
        try (LuaRuntime rt = new LuaRuntime()) {
            rt.doString("java = require 'java'");
            // 设置标准 arg 表
            StringBuilder argSetup = new StringBuilder("arg = {");
            argSetup.append('"').append(path.replace("\\", "\\\\").replace("\"", "\\\"")).append('"');
            for (String a : args) {
                argSetup.append(", ").append('"').append(a.replace("\\", "\\\\").replace("\"", "\\\"")).append('"');
            }
            argSetup.append("}");
            rt.doString(argSetup.toString());
            rt.doFile(path);
        } catch (Exception e) {
            System.err.println(e.getMessage());
        }
    }

    static void repl() throws IOException {
        try (LuaRuntime L = new LuaRuntime()) {
            L.doString("java = require 'java'");
            version();
            System.out.println();
            LineEditor editor = new LineEditor();
            StringBuilder buffer = new StringBuilder();
            int nesting = 0;

            while (true) {
                String prompt = (nesting > 0) ? ">> " : "> ";
                String line;
                try {
                    line = editor.readLine(prompt);
                } catch (UserInterruptException e) {
                    System.out.println("^C");
                    buffer.setLength(0);
                    nesting = 0;
                    continue;
                } catch (EndOfFileException e) {
                    System.out.println();
                    break;
                }
                if (line == null) break;

                line = line.trim();
                if (line.equals("\\q") || line.equals("\\quit")) break;

                if (line.equals("\\h") || line.equals("\\help") || line.equals("help")) {
                    System.out.println("Commands: \\q quit, =expr print result, help/copyright/credits/license");
                    continue;
                }

                if (line.equals("\\copyright") || line.equals("copyright") ||
                    line.equals("\\credits") || line.equals("credits") ||
                    line.equals("\\license") || line.equals("license")) {
                    System.out.println("LuaJavaNE - Lua 5.4.8 + Java interop");
                    System.out.println("Copyright (c) 2026 npp-zep");
                    System.out.println("MIT License. See LICENSE file for details.");
                    System.out.println("Third-party: Lua 5.4.8 (MIT), JLine 3 (BSD-3), JUnit 5 (EPL-1.0)");
                    continue;
                }

                if (line.startsWith("=")) {
                    line = "io.write(tostring(" + line.substring(1) + "), '\\n'); io.flush()";
                }

                buffer.append(line).append("\n");
                nesting += countNesting(line);

                if (nesting <= 0) {
                    String code = buffer.toString();
                    try {
                        L.doString(code);
                        buffer.setLength(0);
                        nesting = 0;
                    } catch (RuntimeException e) {
                        String msg = e.getMessage();
                        if (msg != null && msg.contains("<eof>")) {
                            // 需要更多输入，保留缓冲区，强制继续
                            nesting = 1;
                        } else {
                            System.err.println(msg);
                            buffer.setLength(0);
                            nesting = 0;
                        }
                    }
                }
            }
        } catch (Exception e) {
            System.err.println("REPL error: " + e.getMessage());
        }
    }

    // 简单的嵌套计数（辅助，实际依靠异常检测）
    static int countNesting(String line) {
        String t = line.trim();
        if (t.startsWith("--")) return 0; // 忽略注释行
        int delta = 0;
        if (t.startsWith("function ") || t.startsWith("if ") || t.startsWith("for ") ||
            t.startsWith("while ") || t.startsWith("repeat") || t.startsWith("do ") ||
            t.equals("do") || t.equals("else")) {
            delta++;
        }
        if (t.startsWith("end") || t.startsWith("until ")) {
            delta--;
        }
        return delta;
    }
}