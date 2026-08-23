package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class AsyncTest extends BaseTest {

    @Test
    void asyncConstructor() {
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.String', 'new', 'Hello')");
        L.doString("repeat done, oid = java.checkPromise(id) until done");
        L.doString("obj = java.getObject(oid)");
        L.doString("len = obj:length()");
        assertEquals("5", L.getGlobal("len"));
    }

    @Test
    void asyncStaticMethod() {
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.Integer', 'parseInt', '42')");
        L.doString("repeat done, result = java.checkPromise(id) until done");
        assertEquals("42", L.getGlobal("result"));
    }

    @Test
    void asyncInstanceMethod() {
        L.doString("String = java.import('java.lang.String')");
        L.doString("s = String:new('Hello World')");
        L.doString("id = java.promise()");
        L.doString("java.runAsyncObj(id, s, 'length')");
        L.doString("repeat done, result = java.checkPromise(id) until done");
        assertEquals("11", L.getGlobal("result"));
    }

    @Test
    void asyncArrayResult() {
        // split 返回 String[]，异步结果为对象 ID，经 java.getObject 取回后应为 Java.Array（0 基索引、# 长度）
        L.doString("String = java.import('java.lang.String')");
        L.doString("s = String:new('a,b,c')");
        L.doString("id = java.promise()");
        L.doString("java.runAsyncObj(id, s, 'split', ',')");
        L.doString("repeat done, oid = java.checkPromise(id) until done");
        L.doString("arr = java.getObject(oid)");
        L.doString("function n() return #arr end; function e(i) return arr[i] end");
        assertEquals(3, ((Number) L.callFunction("n")).intValue());
        assertEquals("a", L.callFunction("e", 0));
        assertEquals("c", L.callFunction("e", 2));
    }

    @Test
    void asyncErrorClassNotFound() {
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.NonExistent', 'foo', '')");
        L.doString("repeat done, result = java.checkPromise(id) until done");
        assertTrue(L.getGlobal("result").contains("ClassNotFoundException"));
    }

    @Test
    void asyncErrorMethodNotFound() {
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.String', 'nonExistentMethod', '')");
        L.doString("repeat done, result = java.checkPromise(id) until done");
        assertTrue(L.getGlobal("result").contains("no matching method"));
    }

    @Test
    void asyncConcurrent() {
        int N = 10;
        L.doString("Thread = java.import('java.lang.Thread')");
        L.doString("ids = {}");
        L.doString("for i = 1, " + N + " do ids[i] = java.promise() end");
        L.doString("for i = 1, " + N + " do java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '10') end");
        L.doString("for i = 1, " + N + " do while true do local done = java.checkPromise(ids[i]); if done then break end; Thread.sleep(10) end end");
        assertTrue(true);
    }

    @Test
    void asyncShutdown() {
        L.doString("id = java.promise()");
        L.doString("java.runAsync(id, 'java.lang.Thread', 'sleep', '10')");
        L.close();
        L = null;
        assertTrue(true);
    }
}
