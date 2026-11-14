package com.luajava;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/**
 * table 双向互调的 import 路径测试。
 *
 * <p>与 {@link TableBridgeTest}（{@code registerModule} 注解路径）互补：这里通过
 * {@code java.import("com.luajava.TestBridge")} 调用测试夹具的静态方法，验证普通方法派发下的
 * LuaTable / Map / List 行为。
 */
public class TestBridgeTest extends BaseTest {

    @BeforeEach
    void importBridge() {
        L.doString("TestBridge = java.import('com.luajava.TestBridge')");
    }

    // ========== 方向 A：Lua table -> Java (LuaTable 活引用) ==========

    @Test
    void javaReadsLuaTableFields() {
        L.doString("t = { name='world', n=42, ok=true, 10, 20, 30 }");
        L.doString("function ana() return TestBridge.analyze(t) end");
        assertEquals("name=world n=42 ok=true size=3", L.callFunction("ana"));
    }

    @Test
    void javaCountsLuaTableKeys() {
        L.doString("t = { name='world', n=42, ok=true, 10, 20, 30 }");
        L.doString("function ck() return TestBridge.countKeys(t) end");
        assertEquals(6, ((Number) L.callFunction("ck")).intValue());
    }

    @Test
    void javaWritesLuaTableLiveRef() {
        // Java 写回 Lua table，Lua 侧立即可见（活引用，不复制）
        L.doString("t = { x = 5 }");
        L.doString("function tk() TestBridge.touch(t); return t.x, t.fromJava end");
        Object[] r = L.callFunctionMultiple("tk");
        assertEquals(50, ((Number) r[0]).intValue());
        assertEquals("touched", r[1]);
    }

    // ========== 方向 B：Java Map/List -> Lua (JavaTable 惰性代理) ==========

    @Test
    void luaReadsJavaMapFields() {
        L.doString("m = TestBridge.makeMap()");
        L.doString("function ga() return m.a end; function gb() return m.b end;"
                + " function gc() return m.c end; function ml() return #m end");
        assertEquals(10, ((Number) L.callFunction("ga")).intValue());
        assertEquals(20, ((Number) L.callFunction("gb")).intValue());
        assertEquals(30, ((Number) L.callFunction("gc")).intValue());
        assertEquals(3, ((Number) L.callFunction("ml")).intValue());
    }

    @Test
    void luaIteratesJavaMap() {
        L.doString("m = TestBridge.makeMap()");
        L.doString("function msum() local s=0 for _,v in pairs(m) do s=s+v end return s end");
        assertEquals(60, ((Number) L.callFunction("msum")).intValue());
    }

    @Test
    void luaWritesJavaMap() {
        L.doString("m = TestBridge.makeMap()");
        L.doString("function mw() m.d=99; return m.d end");
        assertEquals(99, ((Number) L.callFunction("mw")).intValue());
    }

    @Test
    void luaReadsAndIteratesJavaList() {
        L.doString("l = TestBridge.makeList()");
        L.doString("function lg() return l[1] .. l[2] .. l[3], #l end");
        Object[] r = L.callFunctionMultiple("lg");
        assertEquals("xyz", r[0]);
        assertEquals(3, ((Number) r[1]).intValue());
        L.doString("function ljoin() local r='' for _,v in ipairs(l) do r=r..v end return r end");
        assertEquals("xyz", L.callFunction("ljoin"));
    }

    @Test
    void luaWritesJavaList() {
        L.doString("l = TestBridge.makeList()");
        L.doString("function lw() l[2]='B'; return l[2] end");
        assertEquals("B", L.callFunction("lw"));
    }
}
