package com.luajava;

import com.luajava.exception.TypeConversionError;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;

public class AsyncRunner {

    // 记录最近一次参数类型转换失败的原因，用于诊断“无匹配方法/构造器”
    private static String lastConversionError = null;

    // 同步调用实例方法（通用 fallback）
    public static String invokeInstance(Object obj, String methodName, String[] args) {
        return invokeInstance(obj, methodName, args, false);
    }

    public static String invokeInstance(Object obj, String methodName, String[] args, boolean expandArrays) {
        if (obj == null) return "E:null instance";
        Class<?> cls = obj.getClass();
        Method best = null;
        Object[] bestArgs = null;
        int bestScore = Integer.MIN_VALUE;
        for (Method m : cls.getMethods()) {
            if (!m.getName().equals(methodName)) continue;
            Object[] converted = matchArgs(m.getParameterTypes(), args);
            if (converted != null) {
                int score = scoreMethodMatch(m.getParameterTypes(), converted);
                if (score > bestScore) {
                    bestScore = score;
                    best = m;
                    bestArgs = converted;
                }
            }
        }
        if (best == null) return "E:no matching method: " + methodName + detail();
        try {
            return serialize(best.invoke(obj, bestArgs), expandArrays);
        } catch (InvocationTargetException e) {
            return "E:" + e.getCause();
        } catch (Throwable e) {
            return "E:" + methodName + " failed -> " + describeError(e);
        }
    }

    public static String runStatic(String className, String methodName, String[] args) {
        return runStatic(className, methodName, args, false);
    }

    public static String runStatic(String className, String methodName, String[] args, boolean expandArrays) {
        try {
            Class<?> cls = Class.forName(className);
            if (methodName.equals("new")) return callConstructor(cls, args);
            return callMethod(cls, null, methodName, args, expandArrays);
        } catch (Throwable e) {
            return "E:" + className + "." + methodName + " -> " + describeError(e);
        }
    }

    public static String runInstance(Object instance, String methodName, String[] args) {
        return invokeInstance(instance, methodName, args, false);
    }

    public static String runInstance(Object instance, String methodName, String[] args, boolean expandArrays) {
        return invokeInstance(instance, methodName, args, expandArrays);
    }

    private static String callConstructor(Class<?> cls, String[] pairs) throws Exception {
        Constructor<?> bestCtor = null;
        Object[] bestArgs = null;
        int bestScore = Integer.MIN_VALUE;
        for (Constructor<?> c : cls.getConstructors()) {
            Object[] converted = matchArgs(c.getParameterTypes(), pairs);
            if (converted != null) {
                int score = scoreMethodMatch(c.getParameterTypes(), converted);
                if (score > bestScore) {
                    bestScore = score;
                    bestCtor = c;
                    bestArgs = converted;
                }
            }
        }
        if (bestCtor == null) return "E:no matching constructor" + detail();
        try {
            Object obj = bestCtor.newInstance(bestArgs);
            int oid = LuaAgent.registerObjectStrong(obj);
            return "O:" + oid;
        } catch (InvocationTargetException e) {
            return "E:" + e.getCause();
        }
    }

    private static String callMethod(Class<?> cls, Object obj, String name, String[] pairs, boolean expandArrays) throws Exception {
        Method best = null;
        Object[] bestArgs = null;
        int bestScore = Integer.MIN_VALUE;
        for (Method m : cls.getMethods()) {
            if (!m.getName().equals(name)) continue;
            Object[] converted = matchArgs(m.getParameterTypes(), pairs);
            if (converted != null) {
                int score = scoreMethodMatch(m.getParameterTypes(), converted);
                if (score > bestScore) {
                    bestScore = score;
                    best = m;
                    bestArgs = converted;
                }
            }
        }
        if (best == null) return "E:no matching method: " + name + detail();
        try {
            return serialize(best.invoke(obj, bestArgs), expandArrays);
        } catch (InvocationTargetException e) {
            return "E:" + e.getCause();
        }
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
            try {
                r[i] = convert(pairs[i * 2], pairs[i * 2 + 1], pt[i]);
                if (r[i] == null) return null;
            } catch (Exception e) {
                lastConversionError = e.getMessage();
                return null;
            }
        }
        return r;
    }

    private static Object convert(String v, String hint, Class<?> t) {
        char h = (hint == null || hint.isEmpty()) ? 'S' : hint.charAt(0);
        // Object 参数按 Lua 原始类型还原，避免数字/布尔被当作字符串传递
        if (t == Object.class) return hintedValue(v, h);
        if (t == String.class) return v;
        try {
            if (t == int.class || t == Integer.class) {
                return h == 'D' ? integralFromDouble(v, Integer.class) : Integer.parseInt(v);
            }
            if (t == long.class || t == Long.class) {
                return h == 'D' ? integralFromDouble(v, Long.class) : Long.parseLong(v);
            }
            if (t == double.class || t == Double.class) return Double.parseDouble(v);
            if (t == float.class || t == Float.class) return Float.parseFloat(v);
            if (t == boolean.class || t == Boolean.class) return Boolean.parseBoolean(v);
            if (t == byte[].class) return v.getBytes();
        } catch (NumberFormatException nfe) {
            throw new TypeConversionError(
                    "cannot convert '" + v + "' to " + t.getSimpleName(), nfe);
        }
        return null;
    }

    // 依据 Lua 侧类型提示（I/D/Z/S）还原 Object 参数；无法解析时退回原始字符串
    private static Object hintedValue(String v, char h) {
        try {
            switch (h) {
                case 'I': return Long.parseLong(v);
                case 'D': return Double.parseDouble(v);
                case 'Z': return Boolean.parseBoolean(v);
                default: return v;
            }
        } catch (NumberFormatException e) {
            return v;
        }
    }

    // Lua 浮点值传给整数参数时，仅接受无小数部分且在范围内的值
    private static Number integralFromDouble(String v, Class<?> t) {
        double d = Double.parseDouble(v);
        if (d != Math.rint(d)) throw new NumberFormatException("not an integer: " + v);
        if (t == Integer.class) {
            if (d < Integer.MIN_VALUE || d > Integer.MAX_VALUE) {
                throw new NumberFormatException("out of int range: " + v);
            }
            return (int) d;
        }
        if (d < Long.MIN_VALUE || d > Long.MAX_VALUE) {
            throw new NumberFormatException("out of long range: " + v);
        }
        return (long) d;
    }

    // 生成可读的错误描述（类型 + 消息 + 根因链），供 E: 字符串携带诊断信息
    private static String describeError(Throwable t) {
        if (t == null) return "null";
        StringBuilder sb = new StringBuilder();
        if (t.getMessage() != null) {
            sb.append(t.getClass().getSimpleName()).append(": ").append(t.getMessage());
        } else {
            sb.append(t.toString());
        }
        Throwable cause = t.getCause();
        int depth = 0;
        while (cause != null && cause != t && depth < 3) {
            sb.append(" <- ").append(cause.getClass().getSimpleName())
              .append(cause.getMessage() != null ? ": " + cause.getMessage() : "");
            cause = cause.getCause();
            depth++;
        }
        return sb.toString();
    }

    // 消费最近一次类型转换失败的原因，返回 " (reason)" 或空串
    private static String detail() {
        String d = lastConversionError;
        lastConversionError = null;
        return d != null ? " (" + d + ")" : "";
    }

    private static String serialize(Object obj) {
        return serialize(obj, false);
    }

    // expandArrays 为 true 时把 Object[] 展开成 M: 多值协议，否则按单个对象注册到对象池。
    private static String serialize(Object obj, boolean expandArrays) {
        if (expandArrays && obj instanceof Object[]) {
            Object[] arr = (Object[]) obj;
            StringBuilder sb = new StringBuilder("M:").append(arr.length).append('|');
            for (Object o : arr) {
                String item = serializeScalar(o);
                // item 形如 "S:xxx"/"I:42"；payload 可能含 ':' 或 '|'，故补长度前缀。
                // 长度按 UTF-8 字节数计算，与 JNI 传给 Lua 的编码一致。
                String payload = item.substring(2);
                sb.append(item.charAt(0)).append(':')
                  .append(payload.getBytes(StandardCharsets.UTF_8).length).append(':')
                  .append(payload);
            }
            return sb.toString();
        }
        return serializeScalar(obj);
    }

    private static String serializeScalar(Object obj) {
        if (obj == null) return "X:nil";
        if (obj instanceof String) return "S:" + obj;
        if (obj instanceof Integer || obj instanceof Long) return "I:" + obj;
        if (obj instanceof Number) return "N:" + obj;
        if (obj instanceof Boolean) return "B:" + obj;
        // 非基本类型：强引用注册到对象池，返回 O:<id>（getObject 取走时释放）
        int oid = LuaAgent.registerObjectStrong(obj);
        return "O:" + oid;
    }

    public static String heavyCalc(String countStr) {
        int n = Integer.parseInt(countStr);
        double sum = 0;
        for (int i = 0; i < n; i++) {
            for (int j = 1; j <= 100; j++) {
                sum += Math.sin(j) * Math.cos(j);
            }
        }
        return String.valueOf(sum);
    }

    /**
     * 判断对象是否是指定类的实例（支持接口）
     * @param obj Java 对象
     * @param className 类名或接口名（全限定名）
     * @return true 如果是该类的实例或实现了该接口
     */
    public static boolean isInstance(Object obj, String className) {
        if (obj == null) return false;
        try {
            Class<?> cls = Class.forName(className);
            return cls.isInstance(obj);
        } catch (ClassNotFoundException e) {
            return false;
        }
    }
}