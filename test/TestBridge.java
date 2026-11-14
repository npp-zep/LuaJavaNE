package com.luajava;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** 测试夹具：table 双向互调的 Java 侧（无字段，纯静态方法，供 Lua 经 java.import 调用）。 */
public class TestBridge {

    /** 方向 A（Lua→Java）：Java 按 LuaTable 惰性读取字段并汇总。 */
    public static String analyze(LuaTable t) {
        return "name=" + t.get("name")
                + " n=" + t.get("n")
                + " ok=" + t.get("ok")
                + " size=" + t.size();
    }

    /** 方向 A 活引用写回：Java 原地修改 table，Lua 侧应立即可见。 */
    public static void touch(LuaTable t) {
        Object x = t.get("x");
        if (x instanceof Number) {
            t.put("x", 10 * ((Number) x).intValue());
        }
        t.put("fromJava", "touched");
    }

    /** Java 侧遍历 Lua table 的键。 */
    public static long countKeys(LuaTable t) {
        return t.keys().length;
    }

    /** 方向 B（Java→Lua）：返回 Map，Lua 侧应得到惰性 JavaTable。 */
    public static Map<String, Integer> makeMap() {
        Map<String, Integer> m = new HashMap<>();
        m.put("a", 10);
        m.put("b", 20);
        m.put("c", 30);
        return m;
    }

    /** 方向 B：返回 List，Lua 侧应得到惰性 JavaTable（列表）。 */
    public static List<String> makeList() {
        List<String> l = new ArrayList<>();
        l.add("x");
        l.add("y");
        l.add("z");
        return l;
    }
}
