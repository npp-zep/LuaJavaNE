package com.luajava;

import java.util.Arrays;

public class TestLuaJava {
    public static void main(String[] args) {
        LuaRuntime L = new LuaRuntime();

        // ============ 原有测试 ============
        System.out.println("=== Java → Lua ===");
        L.setGlobal("msg", "Hello from Java");
        L.doString("print('setGlobal:', msg)");
        L.doString("function add(a, b) return a + b end");
        System.out.println("callFunction: " + L.callFunction("add", 3, 5));

        L.doString("java = require 'java'");

        System.out.println("\n=== Lua → Java ===");
        L.doString("String = java.import('java.lang.String'); s = String:new('Hello World'); print('length:', s:length()); print('substring:', s:substring(0, 5)); print('equals:', s:equals('Hello World'))");

        System.out.println("\n=== Static ===");
        L.doString("Integer = java.import('java.lang.Integer'); print('MAX_VALUE:', Integer.MAX_VALUE); print('parseInt:', Integer.parseInt('42'))");

        System.out.println("\n=== Array ===");
        L.doString("arr = java.newArray('int', 3); arr[0] = 100; arr[1] = 200; arr[2] = 300; print('#arr:', #arr); for i = 0, #arr - 1 do print('  ['..i..'] =', arr[i]) end");

        System.out.println("\n=== Proxy ===");
        L.doString("Runnable = java.import('java.lang.Runnable'); handler = { run = function(self) print('Proxy works!') end }; proxy = java.createProxy({'java.lang.Runnable'}, handler); print('proxy created successfully')");

        // ============ 新增测试 ============

        System.out.println("\n=== LuaFunctionObj 编译与调用 ===");
        L.doString("function multi(a,b) return a+b, a*b, a-b end");
        LuaFunctionObj fn = L.compile("return multi");
        Object[] multiRes = fn.callMultiple(10, 5);
        System.out.println("multi(10,5) returns: " + Arrays.toString(multiRes));
        fn.destroy();
        // 调用后销毁，再次调用应报错
        try {
            fn.call(1,2);
        } catch (IllegalStateException e) {
            System.out.println("Expected error after destroy: " + e.getMessage());
        }

        System.out.println("\n=== 注解模块注册 ===");
        L.registerModule(new MyMath());
        L.doString("print('math_add(3,5):', math_add(3,5))");
        L.doString("print('math_multiply(6,7):', math_multiply(6,7))");
        L.doString("print('math_greet(World):', math_greet('World'))");

        System.out.println("\n=== 字段读写测试 ===");
        L.doString(
            "System = java.import('java.lang.System'); " +
            "print('System.out is:', System.out); " +
            "-- 这里只读取静态字段"
        );

        System.out.println("\n=== 实例字段读写 ===");
        L.doString(
            "Date = java.import('java.util.Date'); " +
            "d = Date:new(); " +
            "print('Initial time:', d:getTime()); " +
            "Point = java.import('java.awt.Point'); " +
            "p = Point:new(10,20); " +
            "print('p.x =', p.x, 'p.y =', p.y); " +
            "p.x = 100; " +
            "p.y = 200; " +
            "print('After assignment: p.x =', p.x, 'p.y =', p.y)"
        );

        System.out.println("\n=== 异步任务测试 ===");
        L.doString(
            "pid = java.promise(); " +
            "java.runAsync(pid, 'com.luajava.AsyncRunner', 'heavyCalc', {'10000'}); " +
            "print('Waiting for async result...'); " +
            "while true do " +
            "  done, result = java.checkPromise(pid); " +
            "  if done then " +
            "    print('Async result:', result); " +
            "    break; " +
            "  end " +
            "end"
        );

        System.out.println("\n=== 异常处理测试 ===");
        L.doString(
            "local ok, err = pcall(function() " +
            "  java.import('java.lang.String'):nonExistingMethod() " +
            "end); " +
            "print('Expected error caught:', err)"
        );

        System.out.println("\n=== 多返回值测试 ===");
        L.doString("function returnThree() return 1, 'two', true end");
        LuaFunctionObj fn3 = L.compile("return returnThree");
        Object[] three = fn3.callMultiple();
        System.out.println("Three returns: " + Arrays.toString(three));
        fn3.destroy();

        System.out.println("\n=== 数组元素类型测试 ===");
        L.doString(
            "strArr = java.newArray('String', 2); " +
            "strArr[0] = 'Hello'; " +
            "strArr[1] = 'Lua'; " +
            "print('String array[0]:', strArr[0], '[1]:', strArr[1])"
        );

        System.out.println("\n=== 复杂对象返回 ===");
        L.doString(
            "Date = java.import('java.util.Date'); " +
            "function getDate() return Date:new() end; " +
            "d = getDate(); " +
            "print('Date object:', d); " +
            "print('Date time:', d:getTime())"
        );

        // 关闭并释放资源
        L.close();
        System.out.println("\n=== All tests passed ===");
    }
}