package com.luajava;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

/**
 * Lua 输入完整性检查器。
 *
 * <p>通过一次词法扫描判断缓冲区是否还处于未闭合状态：
 * <ul>
 *   <li>短字符串（'...' / "..."，含转义）与长括号字符串（[[...]] / [=[...]=]）；</li>
 *   <li>长注释（--[[...]] / --[=[...]=]）与行注释（--）；</li>
 *   <li>圆括号、方括号、花括号是否配平；</li>
 *   <li>if/for/while/do/function/repeat 等语句块是否已由 end/until 收尾。</li>
 * </ul>
 *
 * <p>块结构优先于表达式结构返回：当两者同时未闭合时返回
 * {@link Status#INCOMPLETE_BLOCK}，以便 REPL 启用“空行提交”模式。
 */
public class LuaCompletenessChecker implements InputCompletenessChecker {

    /** 自动缩进宽度（仅空格） */
    private static final int INDENT_WIDTH = 4;

    @Override
    public Status check(String buffer) {
        if (buffer == null || buffer.isEmpty()) {
            return Status.COMPLETE;
        }

        int n = buffer.length();
        Deque<Character> closers = new ArrayDeque<>();
        Deque<String> blocks = new ArrayDeque<>();
        int pendingIf = 0;   // 已见 if 但尚未见 then
        int pendingLoop = 0; // 已见 for/while 但尚未见 do
        boolean unterminated = false;

        int i = 0;
        while (i < n) {
            char c = buffer.charAt(i);

            if (Character.isWhitespace(c)) {
                i++;
                continue;
            }

            // 注释
            if (c == '-' && i + 1 < n && buffer.charAt(i + 1) == '-') {
                i += 2;
                int level = longBracketLevel(buffer, i);
                if (level >= 0) {
                    int end = findLongBracketEnd(buffer, i, level);
                    if (end < 0) {
                        unterminated = true;
                        break;
                    }
                    i = end;
                } else {
                    int nl = buffer.indexOf('\n', i);
                    i = (nl < 0) ? n : nl + 1;
                }
                continue;
            }

            // 短字符串
            if (c == '\'' || c == '"') {
                i++;
                boolean closed = false;
                while (i < n) {
                    char ch = buffer.charAt(i);
                    if (ch == '\\') {
                        i += 2;
                        continue;
                    }
                    if (ch == c) {
                        i++;
                        closed = true;
                        break;
                    }
                    if (ch == '\n') {
                        // Lua 短字符串不允许裸换行；REPL 保持续行编辑，直到用户补上引号
                        unterminated = true;
                        i = n;
                        break;
                    }
                    i++;
                }
                if (!closed) {
                    unterminated = true;
                }
                continue;
            }

            // 长括号字符串或普通方括号
            if (c == '[') {
                int level = longBracketLevel(buffer, i);
                if (level >= 0) {
                    int end = findLongBracketEnd(buffer, i, level);
                    if (end < 0) {
                        unterminated = true;
                        break;
                    }
                    i = end;
                    continue;
                }
                closers.push(']');
                i++;
                continue;
            }

            if (c == '(') {
                closers.push(')');
                i++;
                continue;
            }
            if (c == '{') {
                closers.push('}');
                i++;
                continue;
            }
            if (c == ')' || c == ']' || c == '}') {
                if (!closers.isEmpty() && closers.peek() == c) {
                    closers.pop();
                }
                i++;
                continue;
            }

            if (Character.isLetter(c) || c == '_') {
                int start = i;
                while (i < n && (Character.isLetterOrDigit(buffer.charAt(i)) || buffer.charAt(i) == '_')) {
                    i++;
                }
                String word = buffer.substring(start, i);
                switch (word) {
                    case "if":
                        blocks.push("if");
                        pendingIf++;
                        break;
                    case "then":
                        if (pendingIf > 0) pendingIf--;
                        break;
                    case "for":
                    case "while":
                        blocks.push(word);
                        pendingLoop++;
                        break;
                    case "do":
                        if (pendingLoop > 0) {
                            pendingLoop--;
                        } else {
                            blocks.push("do");
                        }
                        break;
                    case "function":
                        blocks.push("function");
                        break;
                    case "repeat":
                        blocks.push("repeat");
                        break;
                    case "until":
                        if (blocks.peek() != null && blocks.peek().equals("repeat")) {
                            blocks.pop();
                        }
                        break;
                    case "end":
                        if (!blocks.isEmpty()) {
                            blocks.pop();
                        }
                        break;
                    default:
                        break;
                }
                continue;
            }

            i++;
        }

        if (!blocks.isEmpty() || pendingIf > 0 || pendingLoop > 0) {
            return Status.INCOMPLETE_BLOCK;
        }
        if (unterminated || !closers.isEmpty()) {
            return Status.INCOMPLETE_EXPRESSION;
        }
        return Status.COMPLETE;
    }

    /**
     * 依据上一物理行推断下一行的缩进。
     *
     * <p>Lua 没有 Python 那样的冒号缩进规则，这里按语句块关键字判断：
     * then/do/function/repeat 增加一级；以 end/until 开头减少一级；
     * else/elseif 保持当前层级。
     */
    @Override
    public String indentationFor(String buffer) {
        int nl = buffer.lastIndexOf('\n');
        String line = buffer.substring(nl + 1);

        int spaces = 0;
        while (spaces < line.length()) {
            char c = line.charAt(spaces);
            if (c == ' ') {
                spaces++;
            } else if (c == '\t') {
                spaces += INDENT_WIDTH;
            } else {
                break;
            }
        }

        String trimmed = line.strip();
        if (trimmed.isEmpty()) {
            return " ".repeat(spaces);
        }

        List<String> words = words(trimmed);
        if (words.isEmpty()) {
            return " ".repeat(spaces);
        }

        String first = words.get(0);
        String last = words.get(words.size() - 1);

        int delta = 0;
        if (first.equals("end") || first.equals("until")) {
            delta = -1;
        } else if (first.equals("else") || first.equals("elseif")) {
            delta = 0;
        } else if (!last.equals("end") && !last.equals("until")) {
            for (String w : words) {
                if (w.equals("then") || w.equals("do") || w.equals("function") || w.equals("repeat")) {
                    delta = 1;
                    break;
                }
            }
        }

        int width = Math.max(0, spaces + delta * INDENT_WIDTH);
        return " ".repeat(width);
    }

    // ============================================================
    // 词法辅助
    // ============================================================

    /**
     * 若 {@code s[i]} 起始处是长括号（[[ / [=[ / ...），返回等号数量；
     * 否则返回 -1。
     */
    private static int longBracketLevel(String s, int i) {
        if (i >= s.length() || s.charAt(i) != '[') {
            return -1;
        }
        int j = i + 1;
        while (j < s.length() && s.charAt(j) == '=') {
            j++;
        }
        if (j < s.length() && s.charAt(j) == '[') {
            return j - i - 1;
        }
        return -1;
    }

    /**
     * 从长括号起始位置 {@code start} 开始查找对应的结束标记。
     *
     * @return 结束标记之后的下标；未闭合时返回 -1
     */
    private static int findLongBracketEnd(String s, int start, int level) {
        String close = "]" + "=".repeat(level) + "]";
        int idx = s.indexOf(close, start + level + 2);
        return idx < 0 ? -1 : idx + close.length();
    }

    /** 提取一行中的单词（跳过字符串、长括号与注释），用于缩进启发式判断。 */
    private static List<String> words(String line) {
        List<String> result = new ArrayList<>();
        int n = line.length();
        int i = 0;
        while (i < n) {
            char c = line.charAt(i);
            if (c == '-' && i + 1 < n && line.charAt(i + 1) == '-') {
                break;
            }
            if (c == '\'' || c == '"') {
                i++;
                while (i < n) {
                    char ch = line.charAt(i);
                    if (ch == '\\') {
                        i += 2;
                        continue;
                    }
                    if (ch == c) {
                        i++;
                        break;
                    }
                    i++;
                }
                continue;
            }
            if (c == '[') {
                int level = longBracketLevel(line, i);
                if (level >= 0) {
                    int end = findLongBracketEnd(line, i, level);
                    i = (end < 0) ? n : end;
                    continue;
                }
            }
            if (Character.isLetter(c) || c == '_') {
                int start = i;
                while (i < n && (Character.isLetterOrDigit(line.charAt(i)) || line.charAt(i) == '_')) {
                    i++;
                }
                result.add(line.substring(start, i));
                continue;
            }
            i++;
        }
        return result;
    }
}
