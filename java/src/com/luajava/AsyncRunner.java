package com.luajava;

import java.lang.reflect.*;

public class AsyncRunner {
    // 同步调用实例方法（通用 fallback）
    public static String invokeInstance(Object obj, String methodName, String[] args) {
        try {
            if (obj == null) return "E:null instance";
            Class<?> cls = obj.getClass();
            for (Method m : cls.getMethods()) {
                if (!m.getName().equals(methodName)) continue;
                Object[] converted = matchArgs(m.getParameterTypes(), args);
                if (converted != null) {
                    Object result = m.invoke(obj, converted);
                    return serialize(result);
                }
            }
            return "E:no matching method: " + methodName;
        } catch (Throwable e) {
            return "E:" + e.toString();
        }
    }

    public static String runStatic(String className, String methodName, String[] args) {
        try {
            Class<?> cls = Class.forName(className);
            if (methodName.equals("new")) return callConstructor(cls, args);
            return callMethod(cls, null, methodName, args);
        } catch (Throwable e) { return "E:" + e.toString(); }
    }

    public static String runInstance(Object instance, String methodName, String[] args) {
        return invokeInstance(instance, methodName, args);
    }

    private static String callConstructor(Class<?> cls, String[] pairs) throws Exception {
        Constructor<?> bestCtor = null; Object[] bestArgs = null; int bestScore = Integer.MIN_VALUE;
        for (Constructor<?> c : cls.getConstructors()) {
            Object[] converted = matchArgs(c.getParameterTypes(), pairs);
            if (converted != null) {
                int score = scoreMethodMatch(c.getParameterTypes(), converted);
                if (score > bestScore) { bestScore = score; bestCtor = c; bestArgs = converted; }
            }
        }
        if (bestCtor == null) return "E:no matching constructor";
        try { Object obj = bestCtor.newInstance(bestArgs); int oid = LuaAgent.registerObject(obj); return "O:" + oid; }
        catch (InvocationTargetException e) { return "E:" + e.getCause(); }
    }

    private static String callMethod(Class<?> cls, Object obj, String name, String[] pairs) throws Exception {
        Method best = null; Object[] bestArgs = null; int bestScore = Integer.MIN_VALUE;
        for (Method m : cls.getMethods()) {
            if (!m.getName().equals(name)) continue;
            Object[] converted = matchArgs(m.getParameterTypes(), pairs);
            if (converted != null) {
                int score = scoreMethodMatch(m.getParameterTypes(), converted);
                if (score > bestScore) { bestScore = score; best = m; bestArgs = converted; }
            }
        }
        if (best == null) return "E:no matching method: " + name;
        try { return serialize(best.invoke(obj, bestArgs)); }
        catch (InvocationTargetException e) { return "E:" + e.getCause(); }
    }

    private static int scoreMethodMatch(Class<?>[] pts, Object[] args) {
        int score = 0;
        for (int i = 0; i < pts.length; i++) {
            if (args[i] == null) continue;
            if (pts[i].isAssignableFrom(args[i].getClass())) score += 10;
            else if (pts[i] == int.class && args[i] instanceof Integer) score += 8;
            else if (pts[i] == long.class && args[i] instanceof Long) score += 8;
            else if (pts[i] == double.class && args[i] instanceof Double) score += 8;
            else if (pts[i] == boolean.class && args[i] instanceof Boolean) score += 8;
            else if (pts[i] == String.class && args[i] instanceof String) score += 5;
            else score += 1;
        }
        return score;
    }

    private static Object[] matchArgs(Class<?>[] pt, String[] pairs) {
        if (pt.length == 0 && pairs.length == 0) return new Object[0];
        if (pairs.length != pt.length * 2) return null;
        Object[] r = new Object[pt.length];
        for (int i = 0; i < pt.length; i++) {
            try { r[i] = convert(pairs[i*2], pairs[i*2+1], pt[i]); if (r[i] == null) return null; }
            catch (Exception e) { return null; }
        }
        return r;
    }

    private static Object convert(String v, String hint, Class<?> t) {
        if (t == String.class || t == Object.class) return v;
        if (t == int.class || t == Integer.class) return Integer.parseInt(v);
        if (t == long.class || t == Long.class) return Long.parseLong(v);
        if (t == double.class || t == Double.class) return Double.parseDouble(v);
        if (t == float.class || t == Float.class) return Float.parseFloat(v);
        if (t == boolean.class || t == Boolean.class) return Boolean.parseBoolean(v);
        if (t == byte[].class) return v.getBytes();
        return null;
    }

    private static String serialize(Object obj) {
        if (obj == null) return "X:nil";
        if (obj instanceof String) return "S:" + obj;
        if (obj instanceof Integer || obj instanceof Long) return "I:" + obj;
        if (obj instanceof Number) return "N:" + obj;
        if (obj instanceof Boolean) return "B:" + obj;
        // 非基本类型：注册到对象池，返回 O:<id>
        int oid = LuaAgent.registerObject(obj);
        return "O:" + oid;
    }

    public static String heavyCalc(String countStr) {
        int n = Integer.parseInt(countStr);
        double sum = 0;
        for (int i = 0; i < n; i++) for (int j = 1; j <= 100; j++) sum += Math.sin(j) * Math.cos(j);
        return String.valueOf(sum);
    }
}
