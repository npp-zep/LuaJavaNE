package com.luajava;

import java.io.File;
import java.lang.reflect.Method;

public class LuaRuntime {
    private static boolean loaded = false;

    static {
        loadLibrary();
    }

    private static void loadLibrary() {
        if (loaded) return;
        String propPath = System.getProperty("luajava.library.path");
        if (propPath != null && tryLoad(propPath)) return;
        String envPath = System.getenv("LUAJAVA_LIBRARY_PATH");
        if (envPath != null && tryLoad(envPath)) return;
        try { System.loadLibrary("luajava"); loaded = true; return; }
        catch (UnsatisfiedLinkError e) {}
        String cwd = System.getProperty("user.dir");
        for (String name : new String[]{"build/luajava.so", "luajava.so", "libluajava.so"}) {
            if (new File(cwd, name).exists()) { System.load(new File(cwd, name).getAbsolutePath()); loaded = true; return; }
        }
        throw new UnsatisfiedLinkError("Cannot find luajava.so");
    }

    private static boolean tryLoad(String path) {
        try { if (new File(path).exists()) { System.load(new File(path).getAbsolutePath()); loaded = true; return true; } }
        catch (UnsatisfiedLinkError e) {}
        return false;
    }

    long statePtr;

    public LuaRuntime() {
        LuaAgent.init();
        statePtr = _newState();
    }

    public void doString(String script) { _doString(statePtr, script); }
    public void close() { if (statePtr != 0) { _close(statePtr); statePtr = 0; } }
    public void setGlobal(String name, String value) { _setGlobal(statePtr, name, value); }
    public String getGlobal(String name) { return _getGlobal(statePtr, name); }
    public Object callFunction(String name, Object... args) {
        Object[] r = callFunctionMultiple(name, args);
        return (r != null && r.length > 0) ? r[0] : null;
    }
    public native Object[] callFunctionMultiple(String name, Object... args);
    public LuaFunctionObj compile(String code) {
        int ref = _compile(statePtr, code);
        return ref < 0 ? null : new LuaFunctionObj(statePtr, ref);
    }
    public void doFile(String path) { _doFile(statePtr, path); }

    public void registerModule(Object module) {
        Class<?> clazz = module.getClass();
        LuaModule modAnn = clazz.getAnnotation(LuaModule.class);
        String prefix = (modAnn != null && !modAnn.value().isEmpty()) ? modAnn.value() + "_" : "";
        for (Method m : clazz.getDeclaredMethods()) {
            LuaFunction ann = m.getAnnotation(LuaFunction.class);
            if (ann != null) {
                String name = ann.value().isEmpty() ? m.getName() : ann.value();
                _registerCallback(statePtr, prefix + name, new LuaJavaCallback(module, m));
            }
        }
    }

    private native long _newState();
    private native void _doString(long L, String s);
    private native void _close(long L);
    private native void _setGlobal(long L, String n, String v);
    private native String _getGlobal(long L, String n);
    private native int _compile(long L, String code);
    private native void _doFile(long L, String path);
    private native void _registerCallback(long L, String name, LuaJavaCallback cb);
}
