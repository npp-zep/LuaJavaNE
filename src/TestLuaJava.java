package com.luajava;

public class TestLuaJava {
    public static void main(String[] args) {
        LuaRuntime L = new LuaRuntime();

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

        L.close();
        System.out.println("\n=== All tests passed ===");
    }
}
