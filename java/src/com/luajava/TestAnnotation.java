package com.luajava;

@LuaModule("math")
class MyMath {
    @LuaFunction
    public int add(int a, int b) {
        return a + b;
    }

    @LuaFunction("multiply")
    public int mul(int a, int b) {
        return a * b;
    }

    @LuaFunction
    public String greet(String name) {
        return "Hello, " + name + "!";
    }

    // 新增：测试异常抛出
    @LuaFunction
    public int divide(int a, int b) {
        if (b == 0) throw new ArithmeticException("Division by zero");
        return a / b;
    }

    // 新增：测试多参数和返回值
    @LuaFunction
    public double[] stats(double[] values) {
        double sum = 0;
        for (double v : values) sum += v;
        double avg = sum / values.length;
        return new double[]{sum, avg};
    }
}

public class TestAnnotation {
    public static void main(String[] args) {
        LuaRuntime L = new LuaRuntime();
        L.doString("java = require 'java'");
        L.registerModule(new MyMath());

        System.out.println("=== 原有测试 ===");
        L.doString("print('add:', math_add(3, 5))");
        L.doString("print('multiply:', math_multiply(6, 7))");
        L.doString("print('greet:', math_greet('World'))");

        System.out.println("\n=== 新增测试：异常 ===");
        L.doString(
            "local ok, err = pcall(function() " +
            "  math_divide(10, 0) " +
            "end); " +
            "print('Division by zero caught:', err)"
        );

        System.out.println("\n=== 新增测试：数组参数与返回值 ===");
        L.doString(
            "arr = java.newArray('double', 3); " +
            "arr[0] = 1.5; " +
            "arr[1] = 2.5; " +
            "arr[2] = 3.5; " +
            "sum, avg = math_stats(arr); " +
            "print('Sum:', sum, 'Average:', avg)"
        );

        System.out.println("\n=== 新增测试：模块前缀 ===");
        L.doString("print('add (math_add) works:', math_add(10,20))");

        L.close();
        System.out.println("\n=== All annotation tests passed ===");
    }
}