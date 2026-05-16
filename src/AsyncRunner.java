package com.luajava;

import java.lang.reflect.*;

public class AsyncRunner {
    public static String runStatic(String className, String methodName, String[] args) {
        try {
            Class<?> cls = Class.forName(className);
            if (methodName.equals("new")) return callConstructor(cls, args);
            return callMethod(cls, null, methodName, args);
        } catch (Throwable e) {
            return "E:" + e.toString();
        }
    }

    private static String callConstructor(Class<?> cls, String[] args) throws Exception {
        for (Constructor<?> c : cls.getConstructors()) {
            Object[] cv = matchArgs(c.getParameterTypes(), args);
            if (cv != null) {
                try { return serialize(c.newInstance(cv)); }
                catch (InvocationTargetException e) { return "E:" + e.getCause(); }
            }
        }
        return "E:no matching constructor";
    }

    private static String callMethod(Class<?> cls, Object obj, String name, String[] args) throws Exception {
        for (Method m : cls.getMethods()) {
            if (!m.getName().equals(name)) continue;
            Object[] cv = matchArgs(m.getParameterTypes(), args);
            if (cv != null) {
                try { return serialize(m.invoke(obj, cv)); }
                catch (InvocationTargetException e) { return "E:" + e.getCause(); }
            }
        }
        return "E:no matching method: " + name;
    }

    private static Object[] matchArgs(Class<?>[] pt, String[] pairs) {
        if (pt.length == 0 && pairs.length == 0) return new Object[0];
        if (pairs.length != pt.length * 2) return null;
        Object[] r = new Object[pt.length];
        for (int i = 0; i < pt.length; i++) {
            try {
                r[i] = convert(pairs[i*2], pairs[i*2+1], pt[i]);
                if (r[i] == null) return null;
            } catch (Exception e) {
                return null;
            }
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
        return null;
    }

    private static String serialize(Object obj) {
        if (obj == null) return "X:nil";
        if (obj instanceof String) return "S:" + obj;
        if (obj instanceof Integer || obj instanceof Long) return "I:" + obj;
        if (obj instanceof Number) return "N:" + obj;
        if (obj instanceof Boolean) return "B:" + obj;
        return "S:" + obj.toString();
    }

    public static String heavyCalc(String countStr) {
        int n = Integer.parseInt(countStr);
        double sum = 0;
        for (int i = 0; i < n; i++)
            for (int j = 1; j <= 100; j++)
                sum += Math.sin(j) * Math.cos(j);
        return String.valueOf(sum);
    }
}
