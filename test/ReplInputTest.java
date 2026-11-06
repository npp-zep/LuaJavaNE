package com.luajava;

import org.junit.jupiter.api.Test;

import static com.luajava.InputCompletenessChecker.Status.COMPLETE;
import static com.luajava.InputCompletenessChecker.Status.INCOMPLETE_BLOCK;
import static com.luajava.InputCompletenessChecker.Status.INCOMPLETE_EXPRESSION;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * REPL 多行输入判定与缩进的单元测试，不依赖终端。
 */
public class ReplInputTest {

    private final LuaCompletenessChecker checker = new LuaCompletenessChecker();

    // ========== 完整性判定 ==========

    @Test void completeSingleLine() {
        assertEquals(COMPLETE, checker.check("print(1)"));
        assertEquals(COMPLETE, checker.check("if x then print(1) end"));
        assertEquals(COMPLETE, checker.check("local t = {1, 2}"));
        assertEquals(COMPLETE, checker.check("s = 'ok'"));
        assertEquals(COMPLETE, checker.check("-- a comment"));
        assertEquals(COMPLETE, checker.check("a[1] = b[2]"));
        assertEquals(COMPLETE, checker.check(""));
    }

    @Test void incompleteBlocks() {
        assertEquals(INCOMPLETE_BLOCK, checker.check("if x then"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("if x"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("for i=1,3 do"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("for i=1,3"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("while x do"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("function f()"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("repeat"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("if x then\n    print(1)"));
        assertEquals(INCOMPLETE_BLOCK, checker.check("if x then\n    if y then\n    end"));
    }

    @Test void closedMultiLineBlocks() {
        assertEquals(COMPLETE, checker.check("if x then\n    print(1)\nend"));
        assertEquals(COMPLETE, checker.check("for i=1,3 do\n    print(i)\nend"));
        assertEquals(COMPLETE, checker.check("repeat\n    print(1)\nuntil x"));
        assertEquals(COMPLETE, checker.check("function f()\n    return 1\nend"));
        assertEquals(COMPLETE, checker.check("local function f()\nend"));
        assertEquals(COMPLETE, checker.check("do\n    print(1)\nend"));
    }

    @Test void incompleteExpressions() {
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("print("));
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("t = {1, 2"));
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("s = 'abc"));
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("s = \"abc\\"));
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("s = [[abc"));
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("s = [=[abc"));
        assertEquals(INCOMPLETE_EXPRESSION, checker.check("--[[ comment"));
    }

    @Test void closedExpressions() {
        assertEquals(COMPLETE, checker.check("print(1)"));
        assertEquals(COMPLETE, checker.check("print(\n    1\n)"));
        assertEquals(COMPLETE, checker.check("s = [[a\nb]]"));
        assertEquals(COMPLETE, checker.check("s = [=[ a ]] b ]=]"));
        assertEquals(COMPLETE, checker.check("--[[ a\nb ]]"));
        assertEquals(COMPLETE, checker.check("print('a(b')"));
        assertEquals(COMPLETE, checker.check("print(\"a(b\")"));
    }

    @Test void blockTakesPrecedenceOverExpression() {
        assertEquals(INCOMPLETE_BLOCK, checker.check("if x then\n    print("));
        assertEquals(INCOMPLETE_BLOCK, checker.check("function f()\n    s = [[abc"));
    }

    // ========== 自动缩进 ==========

    @Test void indentAfterBlockOpeners() {
        assertEquals("    ", checker.indentationFor("if x then"));
        assertEquals("    ", checker.indentationFor("for i=1,3 do"));
        assertEquals("    ", checker.indentationFor("while x do"));
        assertEquals("    ", checker.indentationFor("function f()"));
        assertEquals("    ", checker.indentationFor("local function f()"));
        assertEquals("    ", checker.indentationFor("repeat"));
        assertEquals("    ", checker.indentationFor("if x then\n    print(1)"));
    }

    @Test void dedentAfterBlockClose() {
        assertEquals("", checker.indentationFor("end"));
        assertEquals("", checker.indentationFor("until x"));
        assertEquals("", checker.indentationFor("if x then\n    print(1)\n    end"));
        assertEquals("    ", checker.indentationFor("if x then\n    print(1)\n    else"));
        assertEquals("    ", checker.indentationFor("if x then\n    print(1)\n    elseif y then"));
    }

    @Test void indentIgnoresKeywordsInStringsAndComments() {
        assertEquals("", checker.indentationFor("print(\"then\")"));
        assertEquals("", checker.indentationFor("print('end')"));
        assertEquals("", checker.indentationFor("print(1) -- end"));
        assertEquals("  ", checker.indentationFor("  print(1)"));
    }

    // ========== 提交策略 ==========

    @Test void submitSingleLineWithoutBlankLine() {
        assertTrue(LineEditor.shouldSubmit("print(1)", 8, false, COMPLETE));
    }

    @Test void neverSubmitIncompleteInput() {
        assertFalse(LineEditor.shouldSubmit("if x then", 9, true, INCOMPLETE_BLOCK));
        assertFalse(LineEditor.shouldSubmit("print(", 6, false, INCOMPLETE_EXPRESSION));
        // 空行也不能绕过未闭合结构
        String unclosed = "if x then\n    print(\"";
        assertFalse(LineEditor.shouldSubmit(unclosed, unclosed.length(), true, INCOMPLETE_EXPRESSION));
    }

    @Test void blockRequiresBlankLineAfterClose() {
        String closed = "if x then\n    print(1)\nend";
        assertFalse(LineEditor.shouldSubmit(closed, closed.length(), true, COMPLETE));
        String closedWithBlank = closed + "\n";
        assertTrue(LineEditor.shouldSubmit(closedWithBlank, closedWithBlank.length(), true, COMPLETE));
        String closedWithIndentedBlank = closed + "\n    ";
        assertTrue(LineEditor.shouldSubmit(closedWithIndentedBlank, closedWithIndentedBlank.length(), true, COMPLETE));
    }

    @Test void expressionContinuationSubmitsAsSoonAsClosed() {
        String closed = "print(\n    1\n)";
        assertTrue(LineEditor.shouldSubmit(closed, closed.length(), false, COMPLETE));
    }

    @Test void enterInMiddleOfBufferOnlySplitsLine() {
        String text = "if x then\n    print(1)\nend";
        assertFalse(LineEditor.shouldSubmit(text, 5, true, COMPLETE));
    }
}
