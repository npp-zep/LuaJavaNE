package com.luajava;

import java.io.*;
import java.util.ArrayList;

public class LineEditor {
    private BufferedReader reader;
    private StringBuilder line = new StringBuilder();
    private int cursor = 0;
    private boolean running = true;
    private ArrayList<String> history = new ArrayList<>();
    private int historyIndex = -1;
    private String savedLine = "";

    public LineEditor(InputStream in) {
        this.reader = new BufferedReader(new InputStreamReader(in));
    }

    private void enableRawMode() {
        try {
            new ProcessBuilder("sh", "-c", "stty -echo raw < /dev/tty")
                .inheritIO().start().waitFor();
        } catch (Exception ignored) {}
    }

    private void disableRawMode() {
        try {
            new ProcessBuilder("sh", "-c", "stty echo -raw < /dev/tty")
                .inheritIO().start().waitFor();
            // 等待终端恢复
            Thread.sleep(10);
        } catch (Exception ignored) {}
    }

    public String readLine(String prompt) throws IOException {
        enableRawMode();
        line.setLength(0);
        cursor = 0;
        running = true;
        historyIndex = -1;
        savedLine = "";
        System.out.print(prompt);
        System.out.flush();

        try {
            while (running) {
                int c = reader.read();
                if (c == -1) { running = false; break; }

                if (c == 3) { System.out.println("^C"); line.setLength(0); break; }
                if (c == 4) { System.out.println(); return null; }

                if (c == '\n' || c == '\r') {
                    System.out.print("\r\n");
                    break;
                } else if (c == 127 || c == 8) {
                    if (cursor > 0) {
                        cursor--;
                        line.deleteCharAt(cursor);
                        redraw(prompt);
                    }
                } else if (c == 27) {
                    int c2 = reader.read();
                    if (c2 == '[') {
                        int c3 = reader.read();
                        if (c3 == 'D') {
                            if (cursor > 0) { cursor--; System.out.print("\033[D"); System.out.flush(); }
                        } else if (c3 == 'C') {
                            if (cursor < line.length()) { cursor++; System.out.print("\033[C"); System.out.flush(); }
                        } else if (c3 == 'A') {
                            if (historyIndex == -1) savedLine = line.toString();
                            if (!history.isEmpty() && historyIndex < history.size() - 1) {
                                historyIndex++;
                                line.setLength(0);
                                line.append(history.get(history.size() - 1 - historyIndex));
                                cursor = line.length();
                                redraw(prompt);
                            }
                        } else if (c3 == 'B') {
                            if (historyIndex > 0) {
                                historyIndex--;
                                line.setLength(0);
                                line.append(history.get(history.size() - 1 - historyIndex));
                                cursor = line.length();
                                redraw(prompt);
                            } else if (historyIndex == 0) {
                                historyIndex = -1;
                                line.setLength(0);
                                line.append(savedLine);
                                cursor = line.length();
                                redraw(prompt);
                            }
                        } else if (c3 == '3') {
                            reader.read();
                            if (cursor < line.length()) {
                                line.deleteCharAt(cursor);
                                redraw(prompt);
                            }
                        }
                    }
                } else if (c == 1) {
                    System.out.print("\033[" + cursor + "D");
                    cursor = 0;
                } else if (c == 5) {
                    System.out.print("\033[" + (line.length() - cursor) + "C");
                    cursor = line.length();
                } else if (c >= 32 && c < 127) {
                    line.insert(cursor, (char) c);
                    cursor++;
                    redraw(prompt);
                }
            }
        } finally {
            disableRawMode();
        }
        String result = line.toString();
        if (!result.isEmpty()) {
            history.add(result);
        }
        return result;
    }

    private void redraw(String prompt) {
        System.out.print("\r\033[K" + prompt + line.toString());
        int diff = line.length() - cursor;
        if (diff > 0) {
            System.out.print("\033[" + diff + "D");
        }
        System.out.flush();
    }
}
