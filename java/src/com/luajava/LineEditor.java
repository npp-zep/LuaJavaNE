package com.luajava;

import java.io.*;
import java.util.*;
import org.jline.keymap.KeyMap;
import org.jline.terminal.*;
import org.jline.utils.Curses;
import org.jline.utils.InfoCmp;
import org.jline.reader.*;

public class LineEditor {
    private LineReader reader;
    private BufferedReader fallbackReader;
    private boolean useFallback;

    private static final String ACCEPT_WIDGET = "luajava-accept-line";

    private final InputCompletenessChecker checker;
    // 本次 readLine 是否因未闭合语句块而进入续行（块闭合后仍需空行确认提交）
    private boolean blockContinuation;

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
        this(new LuaCompletenessChecker());
    }

    public LineEditor(InputCompletenessChecker checker) {
        this.checker = checker != null ? checker : new LuaCompletenessChecker();
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
                // 多行缓冲区中第 2..n 行使用续行提示符；提示符不属于缓冲区内容
                .variable(LineReader.SECONDARY_PROMPT_PATTERN, ">> ")
                .option(LineReader.Option.DISABLE_EVENT_EXPANSION, true)
                .completer(this::complete)
                .build();
            // 用自定义 widget 接管回车：决定插入换行继续编辑，还是提交整个逻辑块
            Widget accept = this::acceptLine;
            reader.getWidgets().put(ACCEPT_WIDGET, accept);
            KeyMap<Binding> mainKeys = reader.getKeyMaps().get(LineReader.MAIN);
            mainKeys.bind(accept, "\r");
            mainKeys.bind(accept, "\n");
            bindArrowKeys(terminal, mainKeys);
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
                        // complete=false：JLine 不会在补全后自动追加空格，是否输入参数由用户决定
                        candidates.add(new Candidate(full, full, null, null, null, null, false));
                    }
                }
            }
            return;
        }

        // 普通单词：关键字 + 基础库函数 + 模块名（补全到 "module."）
        for (String kw : LUA_KEYWORDS) {
            if (kw.startsWith(word)) {
                candidates.add(new Candidate(kw, kw, null, null, null, null, false));
            }
        }

        for (String fn : BASE_FUNCTIONS) {
            if (fn.startsWith(word)) {
                candidates.add(new Candidate(fn, fn, null, null, null, null, false));
            }
        }

        for (String mod : MODULE_FUNCTIONS.keySet()) {
            if (mod.startsWith(word)) {
                String full = mod + ".";
                candidates.add(new Candidate(full, full, null, null, null, null, false));
            }
        }
    }

    public String readLine(String prompt) throws IOException {
        blockContinuation = false;
        if (useFallback) {
            return readFallback(prompt);
        } else {
            return reader.readLine(prompt);
        }
    }

    // ============================================================
    // 多行编辑与提交判定
    // ============================================================

    /**
     * JLine 默认把方向键绑到 up-line-or-search / down-line-or-search：
     * 在缓冲区首/末行时按前缀搜索历史。需求要求的是“到边界后切上一条/下一条历史”，
     * 因此显式改为 up-line-or-history / down-line-or-history。
     */
    private static void bindArrowKeys(Terminal terminal, KeyMap<Binding> keys) {
        // terminfo 返回的是含 "\E" 转义的字面串，必须经 tputs 解释才是真正的 ESC 序列
        String up = keySequence(terminal, InfoCmp.Capability.key_up);
        String down = keySequence(terminal, InfoCmp.Capability.key_down);
        if (up != null) {
            keys.bind(new Reference(LineReader.UP_LINE_OR_HISTORY), up);
        }
        if (down != null) {
            keys.bind(new Reference(LineReader.DOWN_LINE_OR_HISTORY), down);
        }
        // 兼容不发送 application cursor 序列的终端
        keys.bind(new Reference(LineReader.UP_LINE_OR_HISTORY), "\033[A");
        keys.bind(new Reference(LineReader.DOWN_LINE_OR_HISTORY), "\033[B");
    }

    private static String keySequence(Terminal terminal, InfoCmp.Capability capability) {
        String raw = terminal.getStringCapability(capability);
        return raw == null ? null : Curses.tputs(raw);
    }

    /**
     * 接管回车键。
     * <ul>
     *   <li>输入未完整 → 插入换行（缩进由检查器给出），继续编辑；</li>
     *   <li>单行已完整 → 直接提交；</li>
     *   <li>因语句块进入续行 → 闭合后仍需一个空行确认提交；</li>
     *   <li>因括号/字符串进入续行 → 闭合后立即提交。</li>
     * </ul>
     */
    private boolean acceptLine() {
        Buffer buffer = reader.getBuffer();
        String text = buffer.toString();
        int cursor = buffer.cursor();

        // 光标不在末尾时，回车只断行，不提交
        if (cursor < text.length()) {
            buffer.write("\n");
            return true;
        }

        InputCompletenessChecker.Status status = checker.check(text);
        if (shouldSubmit(text, cursor, blockContinuation, status)) {
            blockContinuation = false;
            reader.callWidget(LineReader.ACCEPT_LINE);
            return true;
        }

        if (status == InputCompletenessChecker.Status.INCOMPLETE_BLOCK) {
            blockContinuation = true;
        }
        // 仅在语句块内自动缩进，避免把缩进写进未闭合的字符串/长括号内容
        String indent = (status == InputCompletenessChecker.Status.INCOMPLETE_BLOCK)
            ? checker.indentationFor(text)
            : "";
        buffer.write("\n" + indent);
        return true;
    }

    /** 纯函数形式的提交判定，便于单元测试。 */
    static boolean shouldSubmit(String text, int cursor, boolean blockContinuation,
                                InputCompletenessChecker.Status status) {
        if (cursor < text.length()) {
            return false;
        }
        if (status != InputCompletenessChecker.Status.COMPLETE) {
            return false;
        }
        if (!text.contains("\n")) {
            return true;
        }
        if (!blockContinuation) {
            return true;
        }
        return lastLineBlank(text);
    }

    private static boolean lastLineBlank(String text) {
        int nl = text.lastIndexOf('\n');
        return text.substring(nl + 1).trim().isEmpty();
    }

    /**
     * 无 TTY 时的降级路径：逐行读取，但仍按相同的完整性规则累积为整个逻辑块返回。
     * 该路径不支持缓冲区内的光标导航。
     */
    private String readFallback(String prompt) throws IOException {
        StringBuilder sb = new StringBuilder();
        boolean block = false;
        String currentPrompt = prompt;

        while (true) {
            if (currentPrompt != null && !currentPrompt.isEmpty()) {
                System.out.print(currentPrompt);
                System.out.flush();
            }
            String line = fallbackReader.readLine();
            if (line == null) {
                if (sb.length() == 0) {
                    return null;
                }
                return sb.toString();
            }

            sb.append(line);
            String text = sb.toString();
            InputCompletenessChecker.Status status = checker.check(text);
            if (shouldSubmit(text, text.length(), block, status)) {
                return text;
            }

            if (status == InputCompletenessChecker.Status.INCOMPLETE_BLOCK) {
                block = true;
            }
            String indent = (status == InputCompletenessChecker.Status.INCOMPLETE_BLOCK)
                ? checker.indentationFor(text)
                : "";
            sb.append('\n').append(indent);
            currentPrompt = ">> " + indent;
        }
    }
}
