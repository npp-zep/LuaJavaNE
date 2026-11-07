package com.luajava;

/**
 * REPL 输入完整性检查器。
 *
 * <p>LineEditor 在用户按回车时调用 {@link #check(String)} 判断当前缓冲区能否结束输入。
 * 实现方按目标语言的词法/语法规则定制，因此同一套 REPL 框架可复用于 Lua、SQL 等不同语言。
 */
public interface InputCompletenessChecker {

    enum Status {
        /** 输入已完整，可以提交 */
        COMPLETE,
        /** 存在未闭合的表达式结构：字符串、长括号、圆/方/花括号 */
        INCOMPLETE_EXPRESSION,
        /** 存在未闭合的语句块：if/for/while/function/repeat/do 尚未收尾 */
        INCOMPLETE_BLOCK
    }

    /**
     * 检查缓冲区是否完整。
     *
     * @param buffer 当前完整的编辑缓冲区（可能包含多个物理行，以 \n 分隔）
     */
    Status check(String buffer);

    /**
     * 续行时新物理行应插入的缩进（仅空格）。默认不缩进。
     *
     * @param buffer 光标之前的内容
     */
    default String indentationFor(String buffer) {
        return "";
    }
}
