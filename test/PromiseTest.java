package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class PromiseTest extends BaseTest {

    @Test
    void basicAwaitComplete() {
        L.doString(
            "id = java.promise()\n" +
            "co = coroutine.create(function() result = java.await(id) end)\n" +
            "coroutine.resume(co)\n" +
            "java.complete(id, 'hello')\n"
        );
        assertEquals("hello", L.getGlobal("result"));
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

    @Test
    void multipleValues() {
        L.doString(
            "id = java.promise()\n" +
            "co = coroutine.create(function() x, y, z = java.await(id) end)\n" +
            "coroutine.resume(co)\n" +
            "java.complete(id, 1, 2, 3)\n"
        );
        assertEquals("1", L.getGlobal("x"));
        assertEquals("2", L.getGlobal("y"));
        assertEquals("3", L.getGlobal("z"));
    }

    @Test
    void doubleCompleteNoCrash() {
        L.doString(
            "id = java.promise()\n" +
            "java.complete(id, 'ok')\n" +
            "java.complete(id, 'again')\n"
        );
        // just verify no crash // 不会崩溃即可
    }

    @Test
    void promiseNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> {
            L.doString("java.await(999)");
        });
        assertTrue(ex.getMessage().contains("promise not found"));
    }
}
