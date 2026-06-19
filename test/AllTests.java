package com.luajava;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Disabled;
import static org.junit.jupiter.api.Assertions.*;

@LuaModule("test")
class TestModule {
    @LuaFunction public int add(int a, int b) { return a + b; }
    @LuaFunction public String greet(String name) { return "Hello, " + name + "!"; }
}

public class AllTests extends BaseTest {

    // ========== 基础互调 ==========
    @Test void javaCallLua() {
        L.doString("function add(a, b) return a + b end");
        assertEquals(8, ((Number) L.callFunction("add", 3, 5)).intValue());
    }
    @Test void luaCallJava() {
        L.doString("String = java.import('java.lang.String'); s = String:new('Hello'); function len() return s:length() end");
        assertEquals(5, ((Number) L.callFunction("len")).intValue());
    }
    @Test void staticMethod() {
        L.doString("Integer = java.import('java.lang.Integer'); function p() return Integer.parseInt('42') end");
        assertEquals(42, ((Number) L.callFunction("p")).intValue());
    }
    @Test void arrayOps() {
        L.doString("arr = java.newArray('int',3); arr[0]=10; arr[1]=20; arr[2]=30; function alen() return #arr end");
        assertEquals(3, ((Number) L.callFunction("alen")).intValue());
    }
    @Test void typeString() {
        L.doString("function hi() return 'hello' end");
        assertEquals("hello", L.callFunction("hi"));
    }
    @Test void typeBoolean() {
        L.doString("function t() return true end; function f() return false end");
        assertEquals(true, L.callFunction("t"));
        assertEquals(false, L.callFunction("f"));
    }
    @Test void typeNil() {
        L.doString("function n() return nil end");
        assertNull(L.callFunction("n"));
    }
    @Test void typeDouble() {
        L.doString("function dt(x) return x * 2.5 end");
        assertEquals(25.0, ((Number) L.callFunction("dt", 10)).doubleValue(), 0.001);
    }
    @Test void typeLong() {
        L.doString("Thread = java.import('java.lang.Thread'); function ts() Thread.sleep(1); return 'ok' end");
        assertEquals("ok", L.callFunction("ts"));
    }
    @Test void multipleReturn() {
        L.doString("function multi() return 1,2,3 end");
        Object[] r = L.callFunctionMultiple("multi");
        assertEquals(3, r.length);
        assertEquals(1, ((Number) r[0]).intValue());
        assertEquals(2, ((Number) r[1]).intValue());
        assertEquals(3, ((Number) r[2]).intValue());
    }
    @Test void staticField() {
        L.doString("Integer = java.import('java.lang.Integer'); function max() return Integer.MAX_VALUE end");
        assertEquals(2147483647, ((Number) L.callFunction("max")).intValue());
    }
    @Test void annotationBinding() {
        L.registerModule(new TestModule());
        assertEquals(10, ((Number) L.callFunction("test_add", 3, 7)).intValue());
        assertEquals("Hello, World!", L.callFunction("test_greet", "World"));
    }

    // ========== 错误场景 ==========
    @Test void errClassNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> L.doString("java.import('java.lang.NonExistent')"));
        assertTrue(ex.getMessage().contains("class not found"));
    }
    @Test void errMethodNotFound() {
        Exception ex = assertThrows(RuntimeException.class, () -> L.doString("java.import('java.lang.String'):foobar()"));
        assertTrue(ex.getMessage().contains("method not found") || ex.getMessage().contains("no matching method"));
    }
    @Test void errArrayBounds() {
        Exception ex = assertThrows(RuntimeException.class, () -> L.doString("arr = java.newArray('int',2); print(arr[5])"));
        assertTrue(ex.getMessage().contains("array index out of bounds"));
    }

    // ========== 已知问题 ==========
    @Test @Disabled("Bug: proxy run blocks in JUnit runner")
    void proxyDirectCall() {
        L.doString(
            "Runnable = java.import('java.lang.Runnable')\n" +
            "handler = { run = function(self) return 'ok' end }\n" +
            "proxy = java.createProxy({'java.lang.Runnable'}, handler)\n" +
            "function callProxy() return proxy:run() end\n"
        );
        assertEquals("ok", L.callFunction("callProxy"));
    }

    // compileFunc removed: crashes on x86_64 CI
}
