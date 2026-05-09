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

        // 1. 系统属性
        String propPath = System.getProperty("luajava.library.path");
        if (propPath != null && tryLoad(propPath)) return;

        // 2. 环境变量
        String envPath = System.getenv("LUAJAVA_LIBRARY_PATH");
        if (envPath != null && tryLoad(envPath)) return;

        // 3. 系统库名
        try {
            System.loadLibrary("luajava");
            loaded = true;
            return;
        } catch (UnsatisfiedLinkError ignored) {}

        // 4. fallback
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

    // ========== 实例成员 ==========

    long statePtr;
    LuaAgent agent;

    public LuaRuntime() {
        agent = new LuaAgent();
        statePtr = _newState();
    }

    // ========== 基础 API ==========

    public void doString(String script) {
        _doString(statePtr, script);
    }
    /** 主线程轮询：消费完成队列，唤醒等待的协程 */
    public void poll() {
        agent.poll(this);
    }

    /** 提交异步任务到 Agent 线程池 */
    public void submitTask(int promiseId, int funcRef) {
        agent.submitTask(this, promiseId, funcRef);
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

    public Object callFunction(String funcName, Object... args) {
        Object[] results = callFunctionMultiple(funcName, args);
        return (results != null && results.length > 0) ? results[0] : null;
    }

    public native Object[] callFunctionMultiple(String funcName, Object... args);

    public LuaFunctionObj compile(String luaCode) {
        int ref = _compile(statePtr, luaCode);
        if (ref < 0) return null;
        return new LuaFunctionObj(statePtr, ref);
    }

    public void doFile(String path) {
        _doFile(statePtr, path);
    }

    // ========== 注解绑定 ==========

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

    public native void bindJavaMethod(String luaName, Object module, String methodName, Class<?>... paramTypes);
    private native void _registerCallback(long L, String luaName, LuaJavaCallback callback);

    // ========== native 方法 ==========

    private native long _newState();
    private native void _doString(long L, String script);
    private native void _close(long L);
    private native void _setGlobal(long L, String name, String value);
    private native String _getGlobal(long L, String name);
    private native int _compile(long L, String code);
    private native void _doFile(long L, String path);
}