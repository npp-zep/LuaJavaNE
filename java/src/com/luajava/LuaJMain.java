package com.luajava;

import java.io.*;
import org.jline.reader.*;

public class LuaJMain {
    static LuaRuntime L;
    static StringBuilder buffer = new StringBuilder();
    static int nesting = 0;
    static final String VERSION = System.getProperty("luajava.version", "dev");
    static final String NAME = System.getProperty("luajava.name", "LuaJavaNE");
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
        System.out.println("Lua version: " + getLuaVersion());
        System.out.println("[" + javaVendor + " JDK " + javaVer + " on " + osName + " " + osArch + "]");
        System.out.println("Built with: " + getCCVersion());
        System.out.println("Type \"help\", \"credits\" or \"license\" for more information.");
    }

    static String getLuaVersion() {
        LuaRuntime rt = null;
        try {
            rt = new LuaRuntime();
            rt.doString("_VERSION = _VERSION or 'unknown'");
            Object ver = rt.getGlobal("_VERSION");
            return ver != null ? ver.toString() : "unknown";
        } catch (Exception e) {
            return "unknown";
        } finally {
            if (rt != null) {
                try { rt.close(); } catch (Exception ignored) {}
            }
        }
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
        System.out.println("  help, credits, license  show information");
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
        System.out.println("  help, credits, license  show information");
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
            if (line.equals("\\credits") || line.equals("credits")) {
                showCredits();
                continue;
            }
            if (line.equals("\\license") || line.equals("license")) {
                showLicense();
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

    static void showCredits() {
        System.out.println(NAME + " " + VERSION + " - Lua + Java bidirectional engine");
        System.out.println("Credits:");
        System.out.println("  Project Lead: npp-zep");
        System.out.println("  Built on LuaJava - Lua scripting for Java");
        System.out.println("  Uses JLine for REPL line editing");
        System.out.println(URL);
    }

    static void showLicense() {
        // 获取 luaj.sh 所在目录的 LICENSE 文件
        String scriptDir = getScriptDirectory();
        File licenseFile = new File(scriptDir, "LICENSE");
        
        if (!licenseFile.exists()) {
            System.err.println("License file not found: " + licenseFile.getAbsolutePath());
            System.err.println("Default license: MIT License");
            System.err.println("See " + URL + " for license information.");
            return;
        }

        try (BufferedReader reader = new BufferedReader(new FileReader(licenseFile))) {
            String line;
            while ((line = reader.readLine()) != null) {
                System.out.println(line);
            }
        } catch (IOException e) {
            System.err.println("Error reading LICENSE file: " + e.getMessage());
        }
    }

    static String getScriptDirectory() {
        String path = System.getProperty("luaj.script.path");
        if (path != null && !path.isEmpty()) {
            return new File(path).getParent();
        }
        
        // 尝试从类路径获取
        String classPath = System.getProperty("java.class.path");
        if (classPath != null) {
            String[] paths = classPath.split(File.pathSeparator);
            for (String p : paths) {
                File f = new File(p);
                if (f.getName().equals("luajava.jar") || f.getName().contains("luajava")) {
                    return f.getParent();
                }
            }
        }
        
        // 默认返回当前目录
        return System.getProperty("user.dir");
    }

    // 统计一行 Lua 代码的净缩进变化（>0 表示进入多行模式）。
    // 逐 token 扫描并配平同一行内的开启/闭合关键字，例如
    //   if x then print(1) end   -> 0（if..then 与 end 同行抵消）
    //   while c do break end     -> 0（while..do 与 end 同行抵消）
    //   do print('x') end        -> 0
    //   function f() return 1 end-> 0
    // 跳过字符串字面量与 -- 单行注释，避免把 "end" 之类的文本误判。
    static int countNesting(String line) {
        int n = 0;
        int pendingIf = 0;   // 已见 if，等待 then
        int pendingDo = 0;   // 已见 while/for，等待其后的 do
        String t = line.trim();
        int len = t.length();
        int i = 0;
        while (i < len) {
            char c = t.charAt(i);
            if (Character.isWhitespace(c)) { i++; continue; }
            if (c == '-' && i + 1 < len && t.charAt(i + 1) == '-') break; // 单行注释
            if (c == '"' || c == '\'') { // 字符串字面量
                char q = c;
                i++;
                while (i < len) {
                    if (t.charAt(i) == '\\') i++;
                    else if (t.charAt(i) == q) { i++; break; }
                    i++;
                }
                continue;
            }
            if (Character.isLetter(c)) { // 读取标识符/关键字
                int start = i;
                while (i < len && (Character.isLetterOrDigit(t.charAt(i)) || t.charAt(i) == '_')) i++;
                String w = t.substring(start, i);
                switch (w) {
                    case "if": n++; pendingIf++; break;
                    case "then": if (pendingIf > 0) pendingIf--; break;
                    case "while":
                    case "for": n++; pendingDo++; break;
                    case "do":
                        if (pendingDo > 0) pendingDo--;
                        else n++;
                        break;
                    case "function":
                    case "repeat": n++; break;
                    case "until": n--; break;
                    case "end":
                        n--;
                        if (pendingIf > 0) pendingIf--;
                        break;
                    default: break; // elseif / else / local / return 等不改变块深度
                }
                continue;
            }
            i++;
        }
        return n;
    }
}