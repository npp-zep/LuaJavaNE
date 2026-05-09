package com.luajava;

import java.io.File;
import java.lang.reflect.Method;

public class LuaRuntime {
    private static boolean loaded = false;
    private static LuaRuntime mainInstance = null;

    static {
        loadLibrary();
    }

    private static void loadLibrary() {
        if (loaded) return;
        String propPath = System.getProperty("luajava.library.path");
        if (propPath != null && tryLoad(propPath)) return;
        String envPath = System.getenv("LUAJAVA_LIBRARY_PATH");
        if (envPath != null && tryLoad(envPath)) return;
        try {
            System.loadLibrary("luajava");
            loaded = true;
            return;
        } catch (UnsatisfiedLinkError ignored) {}
        String cwd = System.getProperty("user.dir");
        String[] names = {"build/luajava.so", "luajava.so", "libluajava.so"};
        for (String name : names) {
            File f = new File(cwd, name);
            if (f.exists()) {
                System.load(f.getAbsolutePath());
                loaded = true;
                return;
            }
        }
        throw new UnsatisfiedLinkError(
            "Cannot find luajava native library.\n" +
            "  Searched: " + new File(cwd, "build/luajava.so").getAbsolutePath() + "\n" +
            "  Set luajava.library.path system property or LUAJAVA_LIBRARY_PATH env variable."
        );
    }

    private static boolean tryLoad(String path) {
        try {
            File f = new File(path);
            if (f.exists()) {
                System.load(f.getAbsolutePath());
                loaded = true;
                return true;
            }
        } catch (UnsatisfiedLinkError ignored) {}
        return false;
    }

    long statePtr;
    LuaAgent agent;

    public LuaRuntime() {
        agent = new LuaAgent();
        statePtr = _newState();
        mainInstance = this;
        _setAgent(statePtr, agent);
    }

    public void doString(String script) {
        _doString(statePtr, script);
        agent.poll(this);  // 执行异步任务
    }

    public Object callFunction(String funcName, Object... args) {
        Object[] results = callFunctionMultiple(funcName, args);
        agent.poll(this);  // 执行异步任务
        return (results != null && results.length > 0) ? results[0] : null;
    }

    public void poll() {
        agent.poll(this);
    }

    public void close() {
        if (statePtr != 0) {
            agent.shutdown();
            _close(statePtr);
            statePtr = 0;
        }
    }

    public void setGlobal(String name, String value) {
        _setGlobal(statePtr, name, value);
    }

    public String getGlobal(String name) {
        return _getGlobal(statePtr, name);
    }

    public native Object[] callFunctionMultiple(String funcName, Object... args);

    public LuaFunctionObj compile(String luaCode) {
        int ref = _compile(statePtr, luaCode);
        if (ref < 0) return null;
        return new LuaFunctionObj(statePtr, ref);
    }

    public void doFile(String path) {
        _doFile(statePtr, path);
        agent.poll(this);
    }

    public void registerModule(Object module) {
        Class<?> clazz = module.getClass();
        LuaModule modAnn = clazz.getAnnotation(LuaModule.class);
        String prefix = (modAnn != null && !modAnn.value().isEmpty()) ? modAnn.value() + "_" : "";

        for (Method method : clazz.getDeclaredMethods()) {
            LuaFunction ann = method.getAnnotation(LuaFunction.class);
            if (ann != null) {
                String name = ann.value().isEmpty() ? method.getName() : ann.value();
                String luaName = prefix + name;
                LuaJavaCallback callback = new LuaJavaCallback(module, method);
                _registerCallback(statePtr, luaName, callback);
            }
        }
    }

    /** Agent 回调：在主线程执行异步函数并 complete Promise */
    static void executeAsync(int promiseId, long funcRef) {
        if (mainInstance != null && mainInstance.statePtr != 0) {
            mainInstance._executeAsync(mainInstance.statePtr, promiseId, funcRef);
        }
    }

    public native void bindJavaMethod(String luaName, Object module, String methodName, Class<?>... paramTypes);
    private native void _registerCallback(long L, String luaName, LuaJavaCallback callback);
    private native void _executeAsync(long L, int promiseId, long funcRef);

    private native long _newState();
    private native void _setAgent(long L, LuaAgent agent);
    private native void _doString(long L, String script);
    private native void _close(long L);
    private native void _setGlobal(long L, String name, String value);
    private native String _getGlobal(long L, String name);
    private native int _compile(long L, String code);
    private native void _doFile(long L, String path);
}
