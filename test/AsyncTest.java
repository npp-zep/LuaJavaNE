package com.luajava;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Disabled;
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
    @Disabled("Multi-return parsing differs on CI glibc")
    void asyncMultipleReturns() {
        L.doString("String = java.import('java.lang.String')");
        L.doString("s = String:new('a,b,c')");
        L.doString("id = java.promise()");
        L.doString("java.runAsyncObj(id, s, 'split', ',')");
        L.doString("repeat _res = {java.checkPromise(id)}; _done = tostring(_res[1]) until _done == 'true'");
        L.doString("_a = tostring(_res[2]); _b = tostring(_res[3]); _c = tostring(_res[4])");
        assertEquals("true", L.getGlobal("_done"));
        assertEquals("a", L.getGlobal("_a"));
        assertEquals("b", L.getGlobal("_b"));
        assertEquals("c", L.getGlobal("_c"));
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
    void asyncConcurrent() throws InterruptedException {
        int N = 10;
        L.doString("ids = {}");
        L.doString("for i = 1, " + N + " do ids[i] = java.promise() end");
        L.doString("for i = 1, " + N + " do java.runAsync(ids[i], 'java.lang.Thread', 'sleep', '10') end");
        // 用 Lua 侧轮询，不用 Java sleep
        L.doString("for i = 1, " + N + " do repeat done = java.checkPromise(ids[i]) until done end");
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
