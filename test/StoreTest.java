package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class StoreTest extends BaseTest {

    @Test
    void javaStoreAndFetch() {
        L.store("fromJava", 100);
        L.store("msg", "Hello from Java");

        L.doString("print('Lua sees: ' .. java.fetch('msg'))");

        assertEquals(100.0, (Double) L.fetch("fromJava"), 0.001);
        assertEquals("Hello from Java", (String) L.fetch("msg"));
    }

    @Test
    void luaStoreJavaFetch() {
        L.doString("java.store('luaNum', 3.14)");
        L.doString("java.store('luaStr', 'Lua value')");

        assertEquals(3.14, (Double) L.fetch("luaNum"), 0.001);
        assertEquals("Lua value", (String) L.fetch("luaStr"));
    }

    @Test
    void deleteStore() {
        L.store("temp", "to be deleted");
        assertEquals("to be deleted", (String) L.fetch("temp"));

        L.deleteStore("temp");
        assertNull(L.fetch("temp"));
    }

    @Test
    void fetchNonexistent() {
        assertNull(L.fetch("no_such_key"));
    }
}
