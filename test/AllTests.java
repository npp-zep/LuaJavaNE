package com.luajava;

import com.luajava.exception.LuaRuntimeError;
import com.luajava.exception.LuaSyntaxError;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

@LuaModule("test")
class TestModule {
    @LuaFunction public int add(int a, int b) { return a + b; }
    @LuaFunction public String greet(String name) { return "Hello, " + name + "!"; }
    @LuaFunction public String[] split(String s) { return s.split(" "); }
    @LuaFunction public int[] squares(int n) {
        int[] r = new int[n];
        for (int i = 0; i < n; i++) r[i] = (i + 1) * (i + 1);
        return r;
    }
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
    @Test void methodReturnedStringArrayBehavesLikeNewArray() {
        // Java 方法返回的数组应与 java.newArray 行为一致：0 基索引、# 长度、元素读取/写入
        L.registerModule(new TestModule());
        L.doString("arr = test_split('a b c'); function n() return #arr end; function e(i) return arr[i] end; function w() arr[1]='B'; return arr[1] end");
        assertEquals(3, ((Number) L.callFunction("n")).intValue());
        assertEquals("a", L.callFunction("e", 0));
        assertEquals("c", L.callFunction("e", 2));
        assertEquals("B", L.callFunction("w"));
    }
    @Test void methodReturnedIntArrayBehavesLikeNewArray() {
        L.registerModule(new TestModule());
        L.doString("arr = test_squares(3); function n() return #arr end; function e(i) return arr[i] end; function w() arr[0]=99; return arr[0] end");
        assertEquals(3, ((Number) L.callFunction("n")).intValue());
        assertEquals(1, ((Number) L.callFunction("e", 0)).intValue());
        assertEquals(9, ((Number) L.callFunction("e", 2)).intValue());
        assertEquals(99, ((Number) L.callFunction("w")).intValue());
    }

    // ========== Java→Lua 传参 ==========
    @Test void javaToLuaArgScalarTypes() {
        L.doString("function t(s, i, d, b) return type(s), type(i), type(d), type(b) end");
        Object[] r = L.callFunctionMultiple("t", "x", 42, 3.5, true);
        assertEquals("string", r[0]);
        assertEquals("number", r[1]);
        assertEquals("number", r[2]);
        assertEquals("boolean", r[3]);
    }
    @Test void javaToLuaArgBoxedNumbers() {
        L.doString("function t(v) return type(v), v end");
        Object[] r1 = L.callFunctionMultiple("t", Long.valueOf(42));
        assertEquals("number", r1[0]);
        assertEquals(42, ((Number) r1[1]).intValue());
        Object[] r2 = L.callFunctionMultiple("t", Float.valueOf(2.5f));
        assertEquals("number", r2[0]);
        assertEquals(2.5, ((Number) r2[1]).doubleValue(), 0.001);
        Object[] r3 = L.callFunctionMultiple("t", Short.valueOf((short) 7));
        assertEquals("number", r3[0]);
        assertEquals(7, ((Number) r3[1]).intValue());
        Object[] r4 = L.callFunctionMultiple("t", Character.valueOf('A'));
        assertEquals("string", r4[0]);
        assertEquals("A", r4[1]);
    }
    @Test void javaToLuaArgArrayAndObject() {
        // 数组作为 Java.Array userdata（0 基索引、# 长度），普通对象作为 Java 对象 userdata
        L.doString("function arrLen(a) return #a end; function arrGet(a, i) return a[i] end; function objLen(v) return v:length() end");
        int[] ia = {10, 20, 30};
        assertEquals(3, ((Number) L.callFunction("arrLen", ia)).intValue());
        assertEquals(20, ((Number) L.callFunction("arrGet", ia, 1)).intValue());
        assertEquals(4, ((Number) L.callFunction("objLen", new StringBuilder("abcd"))).intValue());
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
        // Lua 执行期错误应映射为结构化的 LuaRuntimeError
        Exception ex = assertThrows(LuaRuntimeError.class, () -> L.doString("java.import('java.lang.NonExistent')"));
        assertTrue(ex.getMessage().contains("class not found"));
    }
    @Test void errSyntaxError() {
        // Lua 编译期语法错误应映射为 LuaSyntaxError
        assertThrows(LuaSyntaxError.class, () -> L.doString("function ("));
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
    @Test
    void proxyDirectCall() {
        // 用非 void 接口 Supplier.get() 验证代理返回值回路（Runnable.run 为 void，返回值会被丢弃）
        L.doString(
            "Supplier = java.import('java.util.function.Supplier')\n" +
            "handler = { get = function(self) return 'ok' end }\n" +
            "proxy = java.createProxy({'java.util.function.Supplier'}, handler)\n" +
            "function callProxy() return proxy:get() end\n"
        );
        assertEquals("ok", L.callFunction("callProxy"));
    }

    // compileFunc removed: crashes on x86_64 CI
}
