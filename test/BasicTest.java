package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class BasicTest extends BaseTest {

    @Test
    void javaCallLua() {
        L.doString("function add(a, b) return a + b end");
        Object result = L.callFunction("add", 3, 5);
        assertEquals(8, ((Number) result).intValue());
    }

    @Test
    void luaCallJava() {
        L.doString(
            "String = java.import('java.lang.String')\n" +
            "s = String:new('Hello')\n" +
            "function getLength() return s:length() end\n"
        );
        Object result = L.callFunction("getLength");
        assertEquals(5, ((Number) result).intValue());
    }

    @Test
    void staticMethod() {
        L.doString(
            "Integer = java.import('java.lang.Integer')\n" +
            "function parseInt() return Integer.parseInt('42') end\n"
        );
        Object result = L.callFunction("parseInt");
        assertEquals(42, ((Number) result).intValue());
    }

    @Test
    void arrayCreateAndAccess() {
        L.doString(
            "arr = java.newArray('int', 3)\n" +
            "arr[0] = 10\n" +
            "arr[1] = 20\n" +
            "arr[2] = 30\n" +
            "function arrLen() return #arr end\n"
        );
        Object result = L.callFunction("arrLen");
        assertEquals(3, ((Number) result).intValue());
    }

    @Test
    void typeMapping() {
        L.doString("function getStr() return 'hello' end");
        Object result = L.callFunction("getStr");
        assertEquals("hello", result);

        L.doString("function isTrue() return true end");
        result = L.callFunction("isTrue");
        assertEquals(true, result);
    }

    @Test
    void errorClassNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> {
            L.doString("java.import('java.lang.NonExistent')");
        });
        assertTrue(ex.getMessage().contains("class not found"));
    }

    @Test
    void errorMethodNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> {
            L.doString("java.import('java.lang.String'):foobar()");
        });
        assertTrue(ex.getMessage().contains("method not found"));
    }

    @Test
    void errorArrayBounds() {
        Exception ex = assertThrows(RuntimeException.class, () -> {
            L.doString("arr = java.newArray('int', 2); print(arr[5])");
        });
        assertTrue(ex.getMessage().contains("array index out of bounds"));
    }
}
