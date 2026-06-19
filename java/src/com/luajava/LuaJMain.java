package com.luajava;

import java.io.*;
import org.jline.reader.*;

public class LuaJMain {
    static LuaRuntime L;
    static StringBuilder buffer = new StringBuilder();
    static int nesting = 0;
    static final String VERSION = System.getProperty("luajava.version", "dev");
    static final String NAME = System.getProperty("luajava.name", "LuaJavaNE");
    static final String COPYRIGHT = System.getProperty("luajava.copyright", "2026 npp-zep");
    static final String LICENSE = System.getProperty("luajava.license", "MIT");
    static final String URL = System.getProperty("luajava.url", "https://github.com/npp-zep/LuaJavaNE");

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
        System.out.println(NAME + " " + VERSION + " (" + buildTime + ")");
        System.out.println("[" + javaVendor + " JDK " + javaVer + " on " + osName + " " + osArch + "]");
        System.out.println("Built with: " + getCCVersion());
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

    static String getCCVersion() {
        try {
            Process p = Runtime.getRuntime().exec(new String[]{"cc", "--version"});
            java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(p.getInputStream()));
            String line = r.readLine();
            r.close();
            p.waitFor();
            return line != null ? line : "Unknown";
        } catch (Exception e) { return "Unknown"; }
    }

    static void help() {
        System.out.println("luaj - " + NAME + " " + VERSION + " (Lua + Java bidirectional engine)");
        System.out.println();
        System.out.println("Usage: luaj [options] [script.lua] [args...]");
        System.out.println();
        System.out.println("Options:");
        System.out.println("  -e <code>       execute Lua code");
        System.out.println("  -v, --version   print version information");
        System.out.println("  -h, --help      show this help");
        System.out.println();
        System.out.println("REPL commands:");
        System.out.println("  \\q, \\quit      exit REPL");
        System.out.println("  \\h, \\help      show this help");
        System.out.println("  =expr           evaluate and print expression");
        System.out.println("  help, copyright, credits, license  show information");
        System.out.println();
        System.out.println("Examples:");
        System.out.println("  luaj -e \"print(1+1)\"");
        System.out.println("  luaj my_script.lua");
        System.out.println("  luaj -v");
        System.out.println();
        System.out.println("GitHub: " + URL);
    }

    static void helpRepl() {
        System.out.println("luaj - " + NAME + " " + VERSION + " (Lua + Java bidirectional engine)");
        System.out.println();
        System.out.println("REPL commands:");
        System.out.println("  \\q, \\quit      exit REPL");
        System.out.println("  \\h, \\help      show this help");
        System.out.println("  =expr           evaluate and print expression");
        System.out.println("  help, copyright, credits, license  show information");
        System.out.println();
        System.out.println("Examples:");
        System.out.println("  > 1+1");
        System.out.println("  2");
        System.out.println("  > java.import(\"java.lang.System\"):getProperty(\"java.version\")");
        System.out.println("  > =42 * 2");
        System.out.println("  84");
        System.out.println();
        System.out.println("GitHub: " + URL);
    }

    static void execString(String code) {
        LuaRuntime rt = new LuaRuntime();
        rt.doString("java = require 'java'");
        try { rt.doString(code); } catch (Exception e) { System.err.println(e.getMessage()); }
        rt.close();
    }

    static void execFile(String path, String[] args) {
        LuaRuntime rt = new LuaRuntime();
        rt.doString("java = require 'java'");
        rt.doString("arg = {}");
        rt.setGlobal("arg0", path);
        for (int i = 0; i < args.length; i++) rt.setGlobal("arg" + (i + 1), args[i]);
        try { rt.doFile(path); } catch (Exception e) { System.err.println(e.getMessage()); }
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
            try { line = editor.readLine(prompt); }
            catch (UserInterruptException e) { System.out.println("^C"); buffer.setLength(0); nesting = 0; continue; }
            catch (EndOfFileException e) { System.out.println(); break; }
            if (line == null) break;
            line = line.trim();
            if (line.equals("\\q") || line.equals("\\quit")) break;

            if (line.equals("\\h") || line.equals("\\help") || line.equals("help")) {
                helpRepl();
                continue;
            }
            if (line.equals("\\copyright") || line.equals("copyright") ||
                line.equals("\\credits") || line.equals("credits") ||
                line.equals("\\license") || line.equals("license")) {
                System.out.println(NAME + " " + VERSION + " - Lua + Java bidirectional engine");
                System.out.println("Copyright (c) " + COPYRIGHT);
                System.out.println(LICENSE + " License. See LICENSE file for details.");
                System.out.println(URL);
                continue;
            }
            if (line.startsWith("=")) {
                line = "io.write(tostring(" + line.substring(1) + "), '\\n'); io.flush()";
            }
            buffer.append(line).append("\n");
            nesting += countNesting(line);
            if (nesting <= 0) {
                try { L.doString(buffer.toString()); } catch (RuntimeException e) { System.err.println(e.getMessage()); }
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
