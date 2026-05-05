package com.luajava;

import java.io.*;
import org.jline.reader.*;

public class LuaJMain {
    static LuaRuntime L;
    static StringBuilder buffer = new StringBuilder();
    static int nesting = 0;

    public static void main(String[] args) {
        if (args.length == 0) { try { repl(); } catch (IOException e) { System.err.println(e.getMessage()); } return; }
        String arg = args[0];
        if (arg.equals("-e") && args.length >= 2) { execString(args[1]); }
        else if (arg.equals("-v") || arg.equals("--version")) { version(); }
        else if (arg.equals("-h") || arg.equals("--help")) { help(); }
        else { execFile(args[0], java.util.Arrays.copyOfRange(args, 1, args.length)); }
    }

    static void version() {
        String buildTime = getBuildTime();
        String javaVer = System.getProperty("java.version");
        String javaVendor = System.getProperty("java.vendor");
        String osName = System.getProperty("os.name");
        String osArch = System.getProperty("os.arch");

        System.out.println("LuaJavaNE (Lua 5.4.8, " + buildTime + ")");
        System.out.println("[" + javaVendor + " JDK " + javaVer + " on " + osName + " " + osArch + "]");
        System.out.println("Type \"help\", \"copyright\", \"credits\" or \"license\" for more information.");
    }

    static String getBuildTime() {
        try {
            java.util.jar.JarFile jar = new java.util.jar.JarFile("luajava.jar");
            java.util.jar.Manifest mf = jar.getManifest();
            if (mf != null && mf.getMainAttributes().getValue("Build-Time") != null) {
                return mf.getMainAttributes().getValue("Build-Time");
            }
            jar.close();
        } catch (Exception ignored) {}
        return new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss").format(new java.util.Date());
    }

    static void help() {
        System.out.println("luaj - LuaJavaNE interpreter (Lua 5.4.8 + Java)");
        System.out.println("Usage: luaj [-e code] [-v] [-h] [script.lua] [args...]");
    }

    static void execString(String code) {
        LuaRuntime rt = new LuaRuntime();
        rt.doString("java = require 'java'");
        try { rt.doString(code); }
        catch (Exception e) { System.err.println(e.getMessage()); }
        rt.close();
    }

    static void execFile(String path, String[] args) {
        LuaRuntime rt = new LuaRuntime();
        rt.doString("java = require 'java'");
        rt.doString("arg = {}");
        rt.setGlobal("arg0", path);
        for (int i = 0; i < args.length; i++) rt.setGlobal("arg" + (i + 1), args[i]);
        try { rt.doFile(path); }
        catch (Exception e) { System.err.println(e.getMessage()); }
        rt.close();
    }

    static void repl() throws IOException {
        L = new LuaRuntime();
        L.doString("java = require 'java'");
        version();
        System.out.println();
        LineEditor editor = new LineEditor();

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
                try { L.doString(buffer.toString()); }
                catch (RuntimeException e) { System.err.println(e.getMessage()); }
                buffer.setLength(0); nesting = 0;
            }
        }
        L.close();
    }

    static int countNesting(String line) {
        int n = 0;
        String t = line.trim();
        if (t.equals("end") || t.startsWith("until ")) n--;
        if (t.equals("do") || t.equals("else")) n++;
        if (t.startsWith("function ") || t.startsWith("if ") || t.startsWith("for ") ||
            t.startsWith("while ") || t.startsWith("repeat")) n++;
        return n;
    }
}
