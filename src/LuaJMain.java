package com.luajava;

import java.io.*;

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
        System.out.println("luaj - LuaJavaNE interpreter v1.0");
        System.out.println("Lua 5.4.8 with Java interop");
        System.out.println("Build: " + getBuildTime());
        System.out.println("JDK: " + System.getProperty("java.version") +
                           " (" + System.getProperty("java.vendor") + ")");
        System.out.println("Author: npp-zep");
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
        System.out.println("Type \\q to quit, \\h for help.");
        LineEditor editor = new LineEditor(System.in);

        while (true) {
            String prompt = ">".repeat(nesting + 1) + " ";;
            String line = editor.readLine(prompt);
            if (line == null) break;

            if (line.isEmpty()) { System.out.println("Type \\q to quit."); continue; }
            line = line.trim();
            if (line.equals("\\q") || line.equals("\\quit")) break;
            if (line.equals("\\h") || line.equals("\\help")) {
                System.out.println("Commands: \\q quit, \\h help, =expr shorthand");
                continue;
            }
            if (line.startsWith("=")) line = "return " + line.substring(1);
            buffer.append(line).append("\n");
            nesting += countNesting(line);
            if (nesting <= 0) {
                execChunk(buffer.toString());
                buffer.setLength(0); nesting = 0;
            }
        }
        try { Runtime.getRuntime().exec(new String[]{"stty", "echo", "-raw", "<", "/dev/tty"}).waitFor(); } catch (Exception ignored) {}
        L.close();
    }

    static void execChunk(String code) {
        code = code.replaceAll("(?m)^\s+", ""); // 去掉每行的前导空格
        L.doString(
            "local fn, err = load(" + quote(code) + ", '=stdin')\n" +
            "if fn then\n" +
            "    local ok, res = pcall(fn)\n" +
            "    if ok then\n" +
            "        if res ~= nil then print(res) end\n" +
            "    else\n" +
            "        print(debug.traceback(res, 0))\n" +
            "    end\n" +
            "else\n" +
            "    print('Syntax:', err)\n" +
            "end"
        );
    }

    static String quote(String s) {
        return "[===[\n" + s + (s.endsWith("\n") ? "" : "\n") + "]===]";
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
