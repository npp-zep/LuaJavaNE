package com.luajava;

import java.io.*;
import org.jline.terminal.*;
import org.jline.reader.*;

public class LineEditor {
    private LineReader reader;

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
        } catch (IOException e) {
            throw new RuntimeException("Failed to initialize JLine terminal", e);
        }
    }

    public String readLine(String prompt) throws IOException {
        return reader.readLine(prompt);
    }
}
