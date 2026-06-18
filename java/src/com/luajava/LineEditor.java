package com.luajava;

import java.io.*;
import org.jline.terminal.*;
import org.jline.reader.*;

public class LineEditor {
    private LineReader reader;
    private BufferedReader fallbackReader;
    private boolean useFallback;

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
                .build();
            useFallback = false;
        } catch (Exception e) {
            // JLine不可用（例如非TTY环境），优雅降级到标准输入
            System.err.println("Warning: JLine terminal unavailable, using fallback input: " + e.getMessage());
            useFallback = true;
            fallbackReader = new BufferedReader(new InputStreamReader(System.in));
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