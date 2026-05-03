package com.luajava;

import java.io.*;

public class LuaJMain {
    static LuaRuntime L;
    static StringBuilder buffer = new StringBuilder();
    static int nesting = 0;

    public static void main(String[] args) {
        if (args.length == 0) { repl(); return; }
        String arg = args[0];
        if (arg.equals("-e") && args.length >= 2) { execString(args[1]); }
        else if (arg.equals("-v") || arg.equals("--version")) { version(); }
        else if (arg.equals("-h") || arg.equals("--help")) { help(); }
        else { execFile(args[0], java.util.Arrays.copyOfRange(args, 1, args.length)); }
    }

    static void version() {
        System.out.println("luaj - LuaJavaNE interpreter v1.0");
        System.out.println("Lua 5.4.8 with Java interop");
        System.out.println("Build: " + getBuildTime());
        System.out.println("JDK: " + System.getProperty("java.version") +
                           " (" + System.getProperty("java.vendor") + ")");
        System.out.println("Author: npp-zep");
    }

    static String getBuildTime() {
        // 从 MANIFEST 或文件读取构建时间，fallback 用编译时的日期
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

    static void repl() {
        L = new LuaRuntime();
        L.doString("java = require 'java'");
        version();
        System.out.println("Type \\q to quit, \\h for help.");
        System.out.print("> ");

        try {
            BufferedReader r = new BufferedReader(new InputStreamReader(System.in));
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.equals("\\q") || line.equals("\\quit")) break;
                if (line.equals("\\h") || line.equals("\\help")) {
                    System.out.println("Commands: \\q quit, \\h help, =expr shorthand for return expr");
                    System.out.print("> "); continue;
                }
                if (line.startsWith("=")) line = "return " + line.substring(1);
                buffer.append(line).append("\n");
                nesting += countNesting(line);
                if (nesting <= 0) {
                    execChunk(buffer.toString());
                    buffer.setLength(0); nesting = 0;
                } else {
                    System.out.print(">> ");
                }
            }
        } catch (IOException e) { System.err.println(e.getMessage()); }
        L.close();
    }

    static void execChunk(String code) {
        L.doString(
            "local fn, err = load(" + quote(code) + ")\n" +
            "if fn then\n" +
            "    local ok, res = pcall(fn)\n" +
            "    if ok then\n" +
            "        if res ~= nil then print(res) end\n" +
            "    else\n" +
            "        print('error: ' .. tostring(res))\n" +
            "    end\n" +
            "else\n" +
            "    print('error: ' .. err)\n" +
            "end"
        );
    }

    static String quote(String s) {
        return "[===[\n" + s + (s.endsWith("\n") ? "" : "\n") + "]===]";
    }

    static int countNesting(String line) {
        int n = 0;
        for (char c : line.toCharArray()) {
            if (c=='('||c=='{'||c=='[') n++;
            if (c==')'||c=='}'||c==']') n--;
        }
        String t = line.trim();
        if (t.equals("end") || t.startsWith("until ")) n--;
        if (t.startsWith("function ") || t.startsWith("if ") || t.startsWith("for ") ||
            t.startsWith("while ") || t.equals("do") || t.startsWith("repeat")) n++;
        return n;
    }
}
