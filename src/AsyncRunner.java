package com.luajava;
import java.lang.reflect.Method;

public class AsyncRunner {
    /** 供 benchmark 用的计算任务 */
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

    /** 工作线程调用：执行静态方法，自动匹配参数类型，返回字符串 */
    public static String runStatic(String className, String methodName, String[] args) throws Exception {
        Class<?> cls = Class.forName(className);
        
        Method matched = null;
        Object[] converted = null;
        for (Method m : cls.getMethods()) {
            if (!m.getName().equals(methodName)) continue;
            if (args.length != m.getParameterCount()) continue;
            
            Object[] cv = new Object[args.length];
            boolean ok = true;
            for (int i = 0; i < args.length; i++) {
                Class<?> pt = m.getParameterTypes()[i];
                try {
                    cv[i] = convert(args[i], pt);
                } catch (Exception e) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                matched = m;
                converted = cv;
                break;
            }
        }
        
        if (matched == null) {
            return "E:no matching method: " + methodName;
        }
        
        Object result = matched.invoke(null, converted);
        return "S:" + (result != null ? result.toString() : "nil");
    }
    
    /** 类型转换 */
    private static Object convert(String s, Class<?> target) {
        if (target == String.class) return s;
        if (target == int.class || target == Integer.class) return Integer.parseInt(s);
        if (target == long.class || target == Long.class) return Long.parseLong(s);
        if (target == double.class || target == Double.class) return Double.parseDouble(s);
        if (target == boolean.class || target == Boolean.class) return Boolean.parseBoolean(s);
        throw new IllegalArgumentException("unsupported type: " + target);
    }
}
