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

    private static final String[] LUA_FUNCTIONS = {
        "print", "type", "tostring", "tonumber", "pairs", "ipairs", "next", "select",
        "require", "error", "assert", "pcall", "xpcall", "setmetatable", "getmetatable",
        "string.len", "string.sub", "string.upper", "string.lower", "string.format",
        "table.insert", "table.remove", "table.sort", "table.concat",
        "math.abs", "math.sin", "math.cos", "math.sqrt", "math.random",
        "java.import", "java.newArray", "java.createProxy",
        "java.promise", "java.runAsync", "java.runAsyncObj", "java.checkPromise", "java.getObject",
        "clac.pi", "clac.sin", "clac.sqrt", "clac.erf", "clac.array", "clac.batch_add"
    };

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

        for (String kw : LUA_KEYWORDS) {
            if (kw.startsWith(word)) {
                candidates.add(new Candidate(kw, kw, null, null, " ", null, true));
            }
        }

        for (String fn : LUA_FUNCTIONS) {
            if (fn.startsWith(word)) {
                candidates.add(new Candidate(fn, fn, null, null, " ", null, true));
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
