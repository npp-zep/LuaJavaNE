package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class PromiseTest extends BaseTest {

    @Test
    void basicAwaitComplete() {
        L.doString(
            "id = java.promise()\n" +
            "co = coroutine.create(function() r = java.await(id) end)\n" +
            "coroutine.resume(co)\n" +
            "java.complete(id, 'hello')\n"
        );
        assertEquals("hello", L.getGlobal("r"));
    }

    @Test
    void multipleValues() {
        L.doString(
            "id = java.promise()\n" +
            "co = coroutine.create(function() x, y = java.await(id) end)\n" +
            "coroutine.resume(co)\n" +
            "java.complete(id, 1, 2)\n"
        );
        assertEquals("1", L.getGlobal("x"));
        assertEquals("2", L.getGlobal("y"));
    }

    @Test
    void doubleCompleteNoCrash() {
        L.doString(
            "id = java.promise()\n" +
            "java.complete(id, 'first')\n" +
            "java.complete(id, 'second')\n"
        );
        // 不崩溃就算通过
    }

    @Test
    void promiseNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> {
            L.doString("java.await(999)");
        });
        assertTrue(ex.getMessage().contains("promise not found"));
    }

    @Test
    void multiplePromises() {
        L.doString(
            "id1 = java.promise()\n" +
            "id2 = java.promise()\n" +
            "co = coroutine.create(function() a = java.await(id1); b = java.await(id2); r1 = a; r2 = b end)\n" +
            "coroutine.resume(co)\n" +
            "java.complete(id1, 'first')\n" +
            "java.complete(id2, 'second')\n"
        );
        assertEquals("first", L.getGlobal("r1"));
        assertEquals("second", L.getGlobal("r2"));
    }
}
