package com.luajava;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/**
 * table 双向互调（惰性活引用）测试。
 *
 * <p>通过 {@code java.import} 之外的 {@link LuaRuntime#registerModule} 回调路径验证：
 * <ul>
 *   <li>方向 A（Lua table → Java）：Java 以 {@link LuaTable} 形参惰性读取/遍历/写回 Lua table，
 *       改动 Lua 侧立即可见；</li>
 *   <li>方向 B（Java Map/List → Lua）：Java 返回值经 {@code java_table_dispatch} 包成惰性
 *       JavaTable，Lua 侧可按 table 语义读取、取长度、遍历并写回。</li>
 * </ul>
 */
@LuaModule("tbt")
class TableBridgeTestModule {
    @LuaFunction public String analyze(LuaTable t) { return t.get("name") + ":" + t.get("n"); }
    @LuaFunction public int countKeys(LuaTable t) { return t.keys().length; }
    @LuaFunction public int size(LuaTable t) { return t.size(); }
    @LuaFunction public void touch(LuaTable t) { t.put("x", 999); }

    @LuaFunction public Map<String, Integer> makeMap() {
        Map<String, Integer> m = new HashMap<>();
        m.put("a", 10); m.put("b", 20); m.put("c", 30);
        return m;
    }
    @LuaFunction public List<String> makeList() {
        List<String> l = new ArrayList<>();
        l.add("x"); l.add("y"); l.add("z");
        return l;
    }
}

public class TableBridgeTest extends BaseTest {

    // ========== 方向 A：Lua table -> Java (LuaTable 活引用) ==========

    @Test
    void javaReadsLuaTableFields() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("t = { name='world', n=42, 10, 20, 30 }");
        L.doString("function ana() return tbt_analyze(t) end");
        assertEquals("world:42", L.callFunction("ana"));
    }

    @Test
    void javaReadsLuaTableSize() {
        // 数组部分长度（Lua # 即 rawlen）
        L.registerModule(new TableBridgeTestModule());
        L.doString("t = { name='world', n=42, 10, 20, 30 }");
        L.doString("function sz() return tbt_size(t) end");
        assertEquals(3, ((Number) L.callFunction("sz")).intValue());
    }

    @Test
    void javaCountsLuaTableKeys() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("t = { name='world', n=42, ok=true, 10, 20, 30 }");
        L.doString("function ck() return tbt_countKeys(t) end");
        assertEquals(6, ((Number) L.callFunction("ck")).intValue());   // name/n/ok + 3 个数组元素
    }

    @Test
    void javaWritesLuaTableLiveRef() {
        // Java 写回 Lua table，Lua 侧立即可见（活引用，不复制）
        L.registerModule(new TableBridgeTestModule());
        L.doString("t = { x = 5 }");
        L.doString("function tk() tbt_touch(t); return t.x end");
        assertEquals(999, ((Number) L.callFunction("tk")).intValue());
    }

    // ========== 方向 B：Java Map/List -> Lua (JavaTable 惰性代理) ==========

    @Test
    void luaReadsJavaMapFields() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("m = tbt_makeMap()");
        L.doString("function ga() return m.a end; function gb() return m.b end; function gc() return m.c end");
        assertEquals(10, ((Number) L.callFunction("ga")).intValue());
        assertEquals(20, ((Number) L.callFunction("gb")).intValue());
        assertEquals(30, ((Number) L.callFunction("gc")).intValue());
    }

    @Test
    void luaTakesJavaMapLength() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("m = tbt_makeMap()");
        L.doString("function ml() return #m end");
        assertEquals(3, ((Number) L.callFunction("ml")).intValue());
    }

    @Test
    void luaIteratesJavaMap() {
        // pairs 遍历 Java Map（含固定序号游标，第二次起 control 为字符串键）
        L.registerModule(new TableBridgeTestModule());
        L.doString("m = tbt_makeMap()");
        L.doString("function msum() local s=0 for k,v in pairs(m) do s=s+v end return s end");
        assertEquals(60, ((Number) L.callFunction("msum")).intValue());
    }

    @Test
    void luaWritesJavaMap() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("m = tbt_makeMap()");
        L.doString("function mw() m.d=99; return m.d end");
        assertEquals(99, ((Number) L.callFunction("mw")).intValue());
    }

    @Test
    void luaReadsJavaList() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("l = tbt_makeList()");
        L.doString("function lg() return l[1] .. l[2] .. l[3] end; function ll() return #l end");
        assertEquals("xyz", L.callFunction("lg"));
        assertEquals(3, ((Number) L.callFunction("ll")).intValue());
    }

    @Test
    void luaIteratesJavaList() {
        // ipairs 遍历 Java List：停止条件为取 t[size+1] 返回 nil（越界不得抛异常）
        L.registerModule(new TableBridgeTestModule());
        L.doString("l = tbt_makeList()");
        L.doString("function ljoin() local r='' for i,v in ipairs(l) do r=r..v end return r end");
        assertEquals("xyz", L.callFunction("ljoin"));
    }

    @Test
    void luaWritesJavaList() {
        L.registerModule(new TableBridgeTestModule());
        L.doString("l = tbt_makeList()");
        L.doString("function lw() l[2]='B'; return l[2] end");
        assertEquals("B", L.callFunction("lw"));
    }
}