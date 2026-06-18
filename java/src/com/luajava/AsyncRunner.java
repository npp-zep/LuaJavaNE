package com.luajava;

import java.lang.reflect.*;
import java.util.*;

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

    public static String runInstance(Object instance, String methodName, String[] args) {
        try {
            if (instance == null) return "E:null instance";
            return callMethod(instance.getClass(), instance, methodName, args);
        } catch (Throwable e) {
            return "E:" + e.toString();
        }
    }

    private static String callConstructor(Class<?> cls, String[] pairs) throws Exception {
        // 选择最匹配的构造方法
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

        if (bestCtor == null) {
            return "E:no matching constructor";
        }

        try {
            Object obj = bestCtor.newInstance(bestArgs);
            int oid = LuaAgent.registerObject(obj);
            return "O:" + oid;
        } catch (InvocationTargetException e) {
            return "E:" + e.getCause();
        }
    }

    private static String callMethod(Class<?> cls, Object obj, String name, String[] pairs) throws Exception {
        // 收集所有匹配的方法
        List<MethodMatch> matches = new ArrayList<>();
        for (Method m : cls.getMethods()) {
            // 过滤桥接方法和合成方法，避免重复
            if (m.isBridge() || m.isSynthetic()) continue;
            if (!m.getName().equals(name)) continue;
            Object[] converted = matchArgs(m.getParameterTypes(), pairs);
            if (converted != null) {
                int score = scoreMethodMatch(m.getParameterTypes(), converted);
                matches.add(new MethodMatch(m, converted, score));
            }
        }

        if (matches.isEmpty()) {
            return "E:no matching method: " + name;
        }

        // 按分数降序排序，选最高分
        matches.sort((a, b) -> Integer.compare(b.score, a.score));
        MethodMatch best = matches.get(0);

        try {
            Object result = best.method.invoke(obj, best.args);
            return serialize(result);
        } catch (InvocationTargetException e) {
            return "E:" + e.getCause();
        }
    }

    // 辅助类
    private static class MethodMatch {
        Method method;
        Object[] args;
        int score;
        MethodMatch(Method m, Object[] args, int score) {
            this.method = m;
            this.args = args;
            this.score = score;
        }
    }

    /**
     * 计算方法参数匹配得分：值越高越匹配。
     * 对于每个参数，如果转换后的对象类型可以直接赋值给目标类型，得1分；
     * 如果目标类型是基本类型且转换后的包装类型可拆箱后赋值，也得1分；
     * 如果目标类型是Object，得0.5分（作为回退）。
     */
    private static int scoreMethodMatch(Class<?>[] paramTypes, Object[] converted) {
        int score = 0;
        for (int i = 0; i < paramTypes.length; i++) {
            if (converted[i] == null) {
                // 如果参数为null，任何引用类型都可接受，但基本类型不行
                if (paramTypes[i].isPrimitive()) {
                    return Integer.MIN_VALUE;
                }
                score += 1; // null 可以赋给任何引用类型
                continue;
            }
            Class<?> argType = converted[i].getClass();
            Class<?> targetType = paramTypes[i];

            // 如果目标类型是基本类型，转换为对应的包装类型
            if (targetType.isPrimitive()) {
                targetType = primitiveToWrapper(targetType);
            }

            // 检查能否赋值
            if (targetType.isAssignableFrom(argType)) {
                // 如果是完全相同的类型，得分更高
                if (targetType.equals(argType)) {
                    score += 3;
                } else {
                    // 有继承关系，得分与距离成反比（简单距离）
                    score += 2;
                }
            } else {
                // 无法赋值，但可能通过自动拆箱/装箱？基本类型与包装类已经处理
                // 如果是Number，允许数值类型的子类型转换
                if (Number.class.isAssignableFrom(targetType) && Number.class.isAssignableFrom(argType)) {
                    score += 1;
                } else {
                    // 不匹配
                    return Integer.MIN_VALUE;
                }
            }
        }
        return score;
    }

    private static Class<?> primitiveToWrapper(Class<?> primitive) {
        if (primitive == int.class) return Integer.class;
        if (primitive == long.class) return Long.class;
        if (primitive == double.class) return Double.class;
        if (primitive == float.class) return Float.class;
        if (primitive == boolean.class) return Boolean.class;
        if (primitive == byte.class) return Byte.class;
        if (primitive == short.class) return Short.class;
        if (primitive == char.class) return Character.class;
        return primitive;
    }

    private static Object[] matchArgs(Class<?>[] pt, String[] pairs) {
        if (pt.length == 0 && pairs.length == 0) return new Object[0];
        // 每个参数需要两个字符串：值和类型提示
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

    /**
     * 转换方法：支持多种类型提示。
     * hint 可以是单字符（'I','D','S','Z','B','F','C','J'）或类名（如 "java.lang.String"）。
     * 如果 hint 为 "null" 则返回 null。
     */
    private static Object convert(String v, String hint, Class<?> targetType) {
        if (v == null) return null;
        if ("null".equals(v)) return null;

        // 根据hint进行转换
        switch (hint) {
            case "I": // Integer
                return Integer.parseInt(v);
            case "J": // Long
                return Long.parseLong(v);
            case "D": // Double
                return Double.parseDouble(v);
            case "F": // Float
                return Float.parseFloat(v);
            case "B": // Byte
                return Byte.parseByte(v);
            case "C": // Character (取第一个字符)
                return v.charAt(0);
            case "Z": // Boolean
                return Boolean.parseBoolean(v);
            case "S": // String
                return v;
            default:
                // 尝试作为类名处理
                try {
                    Class<?> cls = Class.forName(hint);
                    // 仅支持通过字符串构造的简单类型
                    if (cls == String.class) return v;
                    if (cls == Integer.class) return Integer.parseInt(v);
                    if (cls == Long.class) return Long.parseLong(v);
                    if (cls == Double.class) return Double.parseDouble(v);
                    if (cls == Float.class) return Float.parseFloat(v);
                    if (cls == Boolean.class) return Boolean.parseBoolean(v);
                    if (cls == Byte.class) return Byte.parseByte(v);
                    if (cls == Short.class) return Short.parseShort(v);
                    if (cls == Character.class) return v.charAt(0);
                    // 其他类型，尝试通过构造函数或valueOf，但暂不支持
                    return null;
                } catch (ClassNotFoundException e) {
                    return null;
                }
        }
    }

    private static String serialize(Object obj) {
        if (obj == null) return "X:nil";
        if (obj instanceof Object[]) {
            Object[] arr = (Object[]) obj;
            StringBuilder sb = new StringBuilder("M" + arr.length);
            for (Object item : arr) {
                sb.append("|");
                sb.append(serializeSingle(item));
            }
            return sb.toString();
        }
        return serializeSingle(obj);
    }

    private static String serializeSingle(Object obj) {
        if (obj == null) return "X:nil";
        if (obj instanceof String) return "S:" + obj;
        if (obj instanceof Integer || obj instanceof Long) return "I:" + obj;
        if (obj instanceof Number) return "N:" + obj;
        if (obj instanceof Boolean) return "B:" + obj;
        // 对数组调用 serialize 递归
        if (obj.getClass().isArray()) {
            // 处理基本类型数组
            int len = Array.getLength(obj);
            StringBuilder sb = new StringBuilder("M" + len);
            for (int i = 0; i < len; i++) {
                sb.append("|");
                sb.append(serializeSingle(Array.get(obj, i)));
            }
            return sb.toString();
        }
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