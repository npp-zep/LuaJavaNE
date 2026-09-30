package testpkg;

import com.luajava.LuaTable;
import java.util.*;

/** 验证 table 双向互调的桥接测试类（无字段，纯静态方法，供 Lua 调用）。 */
public class TestBridge {

    /** 方向 A（Lua→Java）：Java 按 LuaTable 惰性读取字段并打印。 */
    public static void analyze(LuaTable t) {
        System.out.println("[java] analyze name=" + t.get("name")
                + " n=" + t.get("n")
                + " ok=" + t.get("ok")
                + " size=" + t.size());
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
        Object[] keys = t.keys();
        System.out.println("[java] countKeys(" + keys.length + ") firstKey=" + keys[0]);
        return keys.length;
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