package com.luajava;

import java.io.File;
import java.lang.reflect.Method;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Lua 运行时环境，封装一个 lua_State*。
 * 实现 AutoCloseable 以支持 try-with-resources 自动释放资源。
 * 注意：多个 LuaRuntime 共享同一个全局线程池（LuaAgent），
 * 但关闭单个实例不会关闭线程池，线程池需在程序退出时显式关闭。
 */
public class LuaRuntime implements AutoCloseable {
    private static final Logger LOGGER = Logger.getLogger(LuaRuntime.class.getName());
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
        try {
            System.loadLibrary("luajava");
            loaded = true;
            return;
        } catch (UnsatisfiedLinkError e) {
            // 继续尝试
        }
        String cwd = System.getProperty("user.dir");
        for (String name : new String[]{"build/luajava.so", "luajava.so", "libluajava.so"}) {
            if (new File(cwd, name).exists()) {
                System.load(new File(cwd, name).getAbsolutePath());
                loaded = true;
                return;
            }
        }
        throw new UnsatisfiedLinkError("Cannot find luajava.so");
    }

    private static boolean tryLoad(String path) {
        try {
            if (new File(path).exists()) {
                System.load(new File(path).getAbsolutePath());
                loaded = true;
                return true;
            }
        } catch (UnsatisfiedLinkError e) {
            // 忽略
        }
        return false;
    }

    long statePtr; // 包访问，供 JNI 使用

    public LuaRuntime() {
        LuaAgent.init(); // 确保线程池已初始化（共享）
        statePtr = _newState();
    }

    public void doString(String script) {
        _doString(statePtr, script);
    }

    /**
     * 关闭 Lua 状态，释放本机资源。
     * 多次调用无害。
     */
    @Override
    public void close() {
        if (statePtr != 0) {
            _close(statePtr);
            statePtr = 0;
        }
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (statePtr != 0) {
                LOGGER.warning("LuaRuntime finalized without explicit close() – resource leak");
                close();
            }
        } finally {
            super.finalize();
        }
    }

    public void setGlobal(String name, String value) {
        _setGlobal(statePtr, name, value);
    }

    public String getGlobal(String name) {
        return _getGlobal(statePtr, name);
    }

    public Object callFunction(String name, Object... args) {
        Object[] r = callFunctionMultiple(name, args);
        return (r != null && r.length > 0) ? r[0] : null;
    }

    public native Object[] callFunctionMultiple(String name, Object... args);

    public LuaFunctionObj compile(String code) {
        int ref = _compile(statePtr, code);
        return ref < 0 ? null : new LuaFunctionObj(statePtr, ref);
    }

    public void doFile(String path) {
        _doFile(statePtr, path);
    }

    /**
     * 注册一个带有 @LuaModule 和 @LuaFunction 注解的 Java 对象。
     * 方法将被注册为 Lua 全局函数，前缀为模块名（若指定）。
     */
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

    // ---------- native methods ----------
    private native long _newState();
    private native void _doString(long L, String s);
    private native void _close(long L);
    private native void _setGlobal(long L, String n, String v);
    private native String _getGlobal(long L, String n);
    private native int _compile(long L, String code);
    private native void _doFile(long L, String path);
    private native void _registerCallback(long L, String name, LuaJavaCallback cb);
}