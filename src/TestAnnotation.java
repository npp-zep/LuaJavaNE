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
}

public class TestAnnotation {
    public static void main(String[] args) {
        LuaRuntime L = new LuaRuntime();
        L.registerModule(new MyMath());

        L.doString("print('add:', math_add(3, 5))");
        L.doString("print('multiply:', math_multiply(6, 7))");
        L.doString("print('greet:', math_greet('World'))");

        L.close();
        System.out.println("All annotation tests passed!");
    }
}
