package com.luajava;

import java.lang.reflect.Method;

public class AsyncRunner {
    /** 工作线程调用：执行静态方法，返回字符串 */
    public static String runStatic(String className, String methodName, String arg) throws Exception {
        Class<?> cls = Class.forName(className);
        Method m = cls.getMethod(methodName, String.class);
        Object result = m.invoke(null, arg);
        return result != null ? result.toString() : "nil";
    }
}
