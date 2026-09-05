package com.luajava;

import java.io.*;
import java.util.*;
import org.jline.terminal.*;
import org.jline.reader.*;

public class LineEditor {
    private LineReader reader;
    private BufferedReader fallbackReader;
    private boolean useFallback;

    private static final String[] LUA_KEYWORDS = {
        "function", "end", "if", "then", "else", "elseif", "for", "while", "do", "repeat", "until",
        "local", "return", "break", "nil", "true", "false", "and", "or", "not", "in", "goto"
    };

    // Lua 基础库（全局函数）
    private static final String[] BASE_FUNCTIONS = {
        "print", "type", "tostring", "tonumber", "pairs", "ipairs", "next", "select",
        "require", "error", "assert", "pcall", "xpcall", "warn",
        "setmetatable", "getmetatable", "rawget", "rawset", "rawlen", "rawequal",
        "collectgarbage", "dofile", "load", "loadfile"
    };

    // Lua 标准库模块 + LuaJavaNE 扩展库模块：module -> 函数列表
    private static final Map<String, String[]> MODULE_FUNCTIONS = new LinkedHashMap<>();
    static {
        MODULE_FUNCTIONS.put("string", new String[]{
            "len", "sub", "upper", "lower", "format", "rep", "reverse", "byte", "char",
            "find", "match", "gmatch", "gsub", "dump"
        });
        MODULE_FUNCTIONS.put("table", new String[]{
            "insert", "remove", "sort", "concat", "unpack", "pack", "move", "maxn"
        });
        MODULE_FUNCTIONS.put("math", new String[]{
            "abs", "ceil", "floor", "fmod", "modf", "max", "min", "sqrt", "pow", "exp", "log",
            "log10", "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
            "sinh", "cosh", "tanh", "random", "randomseed", "tointeger", "type", "ult",
            "pi", "huge", "maxinteger", "mininteger"
        });
        MODULE_FUNCTIONS.put("io", new String[]{
            "read", "write", "open", "close", "lines", "input", "output", "tmpfile", "type",
            "flush", "popen"
        });
        MODULE_FUNCTIONS.put("os", new String[]{
            "clock", "date", "difftime", "execute", "exit", "getenv", "remove", "rename",
            "setlocale", "time", "tmpname"
        });
        MODULE_FUNCTIONS.put("coroutine", new String[]{
            "create", "resume", "yield", "status", "wrap", "isyieldable", "running", "close"
        });
        MODULE_FUNCTIONS.put("utf8", new String[]{
            "char", "charpattern", "codepoint", "codes", "len", "offset"
        });
        MODULE_FUNCTIONS.put("debug", new String[]{
            "debug", "sethook", "gethook", "traceback", "getlocal", "setlocal",
            "getupvalue", "setupvalue", "getinfo", "getmetatable", "setmetatable",
            "getregistry", "getuservalue", "setuservalue"
        });
        MODULE_FUNCTIONS.put("package", new String[]{
            "loadlib", "searchpath", "cpath", "path", "loaded", "preload", "searchers", "config"
        });

        // LuaJavaNE 扩展库
        MODULE_FUNCTIONS.put("java", new String[]{
            "import", "toString", "promise", "await", "onComplete", "yield", "createProxy",
            "complete", "newArray", "store", "fetch", "listall", "deleteStore",
            "runAsync", "runAsyncObj", "getObject", "checkPromise"
        });
        MODULE_FUNCTIONS.put("clac", new String[]{
            "pi", "e", "abs", "floor", "ceil", "round", "trunc", "rint", "nearbyint",
            "min", "max", "pow", "sqrt", "cbrt", "hypot",
            "exp", "exp2", "expm1", "log", "log10", "log2", "log1p",
            "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
            "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
            "erf", "erfc", "tgamma", "lgamma", "modf", "frexp", "ldexp", "fmod",
            "remainder", "copysign", "fma", "nan", "isfinite", "isinf", "isnan",
            "random", "seed", "deg", "rad", "array",
            "batch_add", "batch_sub", "batch_mul", "batch_div",
            "batch_sin", "batch_cos", "batch_tan", "batch_asin", "batch_acos", "batch_atan",
            "batch_sinh", "batch_cosh", "batch_tanh", "batch_asinh", "batch_acosh", "batch_atanh",
            "batch_exp", "batch_exp2", "batch_expm1", "batch_log", "batch_log10", "batch_log2", "batch_log1p",
            "batch_floor", "batch_ceil", "batch_round", "batch_trunc", "batch_rint",
            "batch_erf", "batch_erfc", "batch_tgamma", "batch_lgamma",
            "batch_sqrt", "batch_cbrt", "batch_pow", "batch_atan2", "batch_hypot"
        });
        MODULE_FUNCTIONS.put("utils", new String[]{
            "ns_time", "monotonic_time", "timestamp", "timestamp_ms",
            "sleep", "sleep_ms", "sleep_us", "sleep_ns", "timer"
        });
        MODULE_FUNCTIONS.put("gc", new String[]{
            "hold", "holdWeak", "get", "release", "exists", "count", "list", "clear"
        });
    }

    public LineEditor() {
        try {
            Terminal terminal = TerminalBuilder.builder()
                .system(true)
                .streams(System.in, System.out)
                .jansi(false)
                .ffm(false)
                .build();

            reader = LineReaderBuilder.builder()
                .terminal(terminal)
                .variable(LineReader.HISTORY_FILE, null)
                .variable(LineReader.HISTORY_SIZE, 1000)
                .option(LineReader.Option.DISABLE_EVENT_EXPANSION, true)
                .completer(this::complete)
                .build();
            useFallback = false;
        } catch (Exception e) {
            System.err.println("Warning: JLine terminal unavailable, using fallback input: " + e.getMessage());
            useFallback = true;
            fallbackReader = new BufferedReader(new InputStreamReader(System.in));
        }
    }

    private void complete(LineReader lr, ParsedLine line, List<Candidate> candidates) {
        String word = line.word();
        if (word.isEmpty()) return;

        // 点号限定补全：只建议该模块下的函数，如 "string.s" -> string.sub / string.sort
        int dot = word.indexOf('.');
        if (dot >= 0) {
            String[] funcs = MODULE_FUNCTIONS.get(word.substring(0, dot));
            if (funcs != null) {
                String prefix = word.substring(dot + 1);
                for (String f : funcs) {
                    if (f.startsWith(prefix)) {
                        String full = word.substring(0, dot) + "." + f;
                        candidates.add(new Candidate(full, full, null, null, " ", null, true));
                    }
                }
            }
            return;
        }

        // 普通单词：关键字 + 基础库函数 + 模块名（补全到 "module."）
        for (String kw : LUA_KEYWORDS) {
            if (kw.startsWith(word)) {
                candidates.add(new Candidate(kw, kw, null, null, " ", null, true));
            }
        }

        for (String fn : BASE_FUNCTIONS) {
            if (fn.startsWith(word)) {
                candidates.add(new Candidate(fn, fn, null, null, " ", null, true));
            }
        }

        for (String mod : MODULE_FUNCTIONS.keySet()) {
            if (mod.startsWith(word)) {
                String full = mod + ".";
                candidates.add(new Candidate(full, full, null, null, " ", null, true));
            }
        }
    }

    public String readLine(String prompt) throws IOException {
        if (useFallback) {
            if (prompt != null && !prompt.isEmpty()) {
                System.out.print(prompt);
                System.out.flush();
            }
            return fallbackReader.readLine();
        } else {
            return reader.readLine(prompt);
        }
    }
}
