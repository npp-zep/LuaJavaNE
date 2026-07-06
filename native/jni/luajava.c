// jni/luajava.c
#include "luajava.h"
extern int luaopen_java(lua_State* L);
extern void lua_open_custom_libs(lua_State* L);
#include "com_luajava_LuaRuntime.h"
#include "com_luajava_LuaFunctionObj.h"
#include "com_luajava_LuaInvocationHandler.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


//#include <sys/syscall.h>   // for SYS_gettid
//#include <unistd.h>        // for syscall
//#include <errno.h>         // for EBUSY

JavaVM* g_jvm = NULL;
// 在 luajava.c 顶部，将静态初始化改为声明
pthread_mutex_t lua_mutex;   // 不再静态初始化



#define LUA_LOCK()   pthread_mutex_lock(&lua_mutex)
#define LUA_UNLOCK() pthread_mutex_unlock(&lua_mutex)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    
    // 初始化递归互斥锁
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lua_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    
    return JNI_VERSION_1_6;
}

JNIEnv* getEnv() {
    JNIEnv* env = NULL;
    if (g_jvm) {
        (*g_jvm)->GetEnv(g_jvm, (void**)&env, JNI_VERSION_1_6);
        if (!env) {
            JavaVMAttachArgs args;
            args.version = JNI_VERSION_1_6;
            args.group = NULL;
            args.name = NULL;
            (*g_jvm)->AttachCurrentThread(g_jvm, &env, &args);
        }
    }
    return env;
}

void throwLuaError(JNIEnv* env, lua_State* L, int errCode) {
    if (errCode != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         msg ? msg : "Unknown Lua error");
    }
}

// ========== Lua ↔ Java 类型转换 ==========

void push_java_arg(lua_State* L, JNIEnv* env, jobject arg) {
    if (arg == NULL) {
        lua_pushnil(L);
        return;
    }
    if ((*env)->IsInstanceOf(env, arg, (*env)->FindClass(env, "java/lang/String"))) {
        const char* str = (*env)->GetStringUTFChars(env, (jstring)arg, NULL);
        lua_pushstring(L, str);
        (*env)->ReleaseStringUTFChars(env, (jstring)arg, str);
        return;
    }
    if ((*env)->IsInstanceOf(env, arg, (*env)->FindClass(env, "java/lang/Double"))) {
        jclass cls = (*env)->GetObjectClass(env, arg);
        jmethodID mid = (*env)->GetMethodID(env, cls, "doubleValue", "()D");
        lua_pushnumber(L, (*env)->CallDoubleMethod(env, arg, mid));
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    if ((*env)->IsInstanceOf(env, arg, (*env)->FindClass(env, "java/lang/Integer"))) {
        jclass cls = (*env)->GetObjectClass(env, arg);
        jmethodID mid = (*env)->GetMethodID(env, cls, "intValue", "()I");
        lua_pushinteger(L, (*env)->CallIntMethod(env, arg, mid));
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    if ((*env)->IsInstanceOf(env, arg, (*env)->FindClass(env, "java/lang/Boolean"))) {
        jclass cls = (*env)->GetObjectClass(env, arg);
        jmethodID mid = (*env)->GetMethodID(env, cls, "booleanValue", "()Z");
        lua_pushboolean(L, (*env)->CallBooleanMethod(env, arg, mid));
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    lua_pushnil(L);
}

jobject lua_to_java_object(lua_State* L, JNIEnv* env, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNIL: return NULL;
        case LUA_TBOOLEAN: {
            jclass cls = (*env)->FindClass(env, "java/lang/Boolean");
            jmethodID m = (*env)->GetStaticMethodID(env, cls, "valueOf", "(Z)Ljava/lang/Boolean;");
            jobject v = (*env)->CallStaticObjectMethod(env, cls, m, (jboolean)lua_toboolean(L, idx));
            (*env)->DeleteLocalRef(env, cls);
            return v;
        }
        case LUA_TNUMBER: {
            if (lua_isinteger(L, idx)) {
                jclass cls = (*env)->FindClass(env, "java/lang/Integer");
                jmethodID m = (*env)->GetStaticMethodID(env, cls, "valueOf", "(I)Ljava/lang/Integer;");
                jobject v = (*env)->CallStaticObjectMethod(env, cls, m, (jint)lua_tointeger(L, idx));
                (*env)->DeleteLocalRef(env, cls);
                return v;
            } else {
                jclass cls = (*env)->FindClass(env, "java/lang/Double");
                jmethodID m = (*env)->GetStaticMethodID(env, cls, "valueOf", "(D)Ljava/lang/Double;");
                jobject v = (*env)->CallStaticObjectMethod(env, cls, m, (jdouble)lua_tonumber(L, idx));
                (*env)->DeleteLocalRef(env, cls);
                return v;
            }
        }
        case LUA_TSTRING: {
            const char* s = lua_tostring(L, idx);
            return s ? (*env)->NewStringUTF(env, s) : NULL;
        }
        case LUA_TFUNCTION: {
            lua_pushvalue(L, idx);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushstring(L, "luajava_stateptr");
            lua_rawget(L, LUA_REGISTRYINDEX);
            jlong statePtr = (jlong)lua_tointeger(L, -1);
            lua_pop(L, 1);
            jclass cls = (*env)->FindClass(env, "com/luajava/LuaFunctionObj");
            jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(JI)V");
            jobject obj = (*env)->NewObject(env, cls, ctor, statePtr, (jint)ref);
            (*env)->DeleteLocalRef(env, cls);
            return obj;
        }
        default: return NULL;
    }
}

// ========== LuaRuntime._newState ==========
JNIEXPORT jlong JNICALL Java_com_luajava_LuaRuntime__1newState(JNIEnv* env, jclass cls) {
    LUA_LOCK();
    lua_State* L = luaL_newstate();
    if (L) {
        luaL_requiref(L, "_G", luaopen_base, 1);
        lua_pop(L, 1);
        luaL_openlibs(L);
    lua_open_custom_libs(L);
        lua_pushstring(L, "luajava_stateptr");
        lua_pushinteger(L, (lua_Integer)(uintptr_t)L);
        lua_settable(L, LUA_REGISTRYINDEX);
    }
    LUA_UNLOCK();
    return (jlong)(uintptr_t)L;
}

// ========== LuaRuntime._doString ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1doString(JNIEnv* env, jobject obj,
                                                              jlong Lptr, jstring script) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* code = (*env)->GetStringUTFChars(env, script, NULL);
    int err = luaL_dostring(L, code);
    (*env)->ReleaseStringUTFChars(env, script, code);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         msg ? msg : "Unknown Lua error");
        return;
    }
    LUA_UNLOCK();
}

// ========== LuaRuntime._close ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1close(JNIEnv* env, jobject obj, jlong Lptr) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    if (L) lua_close(L);
    LUA_UNLOCK();
}

// ========== LuaRuntime._setGlobal ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1setGlobal(JNIEnv* env, jobject obj,
                                                                jlong Lptr, jstring name,
                                                                jstring value) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* k = (*env)->GetStringUTFChars(env, name, NULL);
    const char* v = (*env)->GetStringUTFChars(env, value, NULL);
    lua_pushstring(L, v);
    lua_setglobal(L, k);
    (*env)->ReleaseStringUTFChars(env, name, k);
    (*env)->ReleaseStringUTFChars(env, value, v);
    LUA_UNLOCK();
}

// ========== LuaRuntime._getGlobal ==========
JNIEXPORT jstring JNICALL Java_com_luajava_LuaRuntime__1getGlobal(JNIEnv* env, jobject obj,
                                                                   jlong Lptr, jstring name) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* k = (*env)->GetStringUTFChars(env, name, NULL);
    lua_getglobal(L, k);
    (*env)->ReleaseStringUTFChars(env, name, k);
    jstring result = NULL;
    if (lua_isstring(L, -1)) {
        result = (*env)->NewStringUTF(env, lua_tostring(L, -1));
    }
    lua_pop(L, 1);
    LUA_UNLOCK();
    return result;
}

// ========== LuaRuntime._compile ==========
JNIEXPORT jint JNICALL Java_com_luajava_LuaRuntime__1compile(JNIEnv* env, jobject obj,
                                                              jlong Lptr, jstring code) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* ccode = (*env)->GetStringUTFChars(env, code, NULL);
    int err = luaL_loadstring(L, ccode);
    (*env)->ReleaseStringUTFChars(env, code, ccode);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         msg ? msg : "compile error");
        return -1;
    }
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    LUA_UNLOCK();
    return ref;
}

// ========== LuaRuntime._doFile ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1doFile(JNIEnv* env, jobject obj,
                                                             jlong Lptr, jstring path) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* cpath = (*env)->GetStringUTFChars(env, path, NULL);
    int err = luaL_dofile(L, cpath);
    (*env)->ReleaseStringUTFChars(env, path, cpath);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         msg ? msg : "Unknown Lua error");
        return;
    }
    LUA_UNLOCK();
}

// ========== LuaRuntime.callFunctionMultiple ==========
JNIEXPORT jobjectArray JNICALL Java_com_luajava_LuaRuntime_callFunctionMultiple
  (JNIEnv* env, jobject obj, jstring funcName, jobjectArray args) {
    LUA_LOCK();
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID f = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jlong Lptr = (*env)->GetLongField(env, obj, f);
    (*env)->DeleteLocalRef(env, cls);

    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* name = (*env)->GetStringUTFChars(env, funcName, NULL);
    lua_getglobal(L, name);
    (*env)->ReleaseStringUTFChars(env, funcName, name);

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"), "not a function");
        return NULL;
    }

    int nargs = 0;
    if (args) {
        nargs = (*env)->GetArrayLength(env, args);
        for (int i = 0; i < nargs; i++) {
            jobject arg = (*env)->GetObjectArrayElement(env, args, i);
            push_java_arg(L, env, arg);
            if (arg) (*env)->DeleteLocalRef(env, arg);
        }
    }

    int err = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         msg ? msg : "Lua error");
        return NULL;
    }

    int nresults = lua_gettop(L);
    jclass objCls = (*env)->FindClass(env, "java/lang/Object");
    jobjectArray results = (*env)->NewObjectArray(env, nresults, objCls, NULL);
    (*env)->DeleteLocalRef(env, objCls);
    for (int i = nresults; i >= 1; i--) {
        jobject val = lua_to_java_object(L, env, -1);
        (*env)->SetObjectArrayElement(env, results, i - 1, val);
        if (val) (*env)->DeleteLocalRef(env, val);
        lua_pop(L, 1);
    }
    LUA_UNLOCK();
    return results;
}

// ========== LuaFunction.callMultiple ==========
JNIEXPORT jobjectArray JNICALL Java_com_luajava_LuaFunctionObj_callMultiple
  (JNIEnv* env, jobject obj, jobjectArray args) {
    LUA_LOCK();
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID sf = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID rf = (*env)->GetFieldID(env, cls, "ref", "I");
    jlong Lptr = (*env)->GetLongField(env, obj, sf);
    jint ref = (*env)->GetIntField(env, obj, rf);
    (*env)->DeleteLocalRef(env, cls);

    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"), "invalid function ref");
        return NULL;
    }

    int nargs = 0;
    if (args) {
        nargs = (*env)->GetArrayLength(env, args);
        for (int i = 0; i < nargs; i++) {
            jobject arg = (*env)->GetObjectArrayElement(env, args, i);
            push_java_arg(L, env, arg);
            if (arg) (*env)->DeleteLocalRef(env, arg);
        }
    }

    int err = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         msg ? msg : "Lua error");
        return NULL;
    }

    int nresults = lua_gettop(L);
    jclass objCls = (*env)->FindClass(env, "java/lang/Object");
    jobjectArray results = (*env)->NewObjectArray(env, nresults, objCls, NULL);
    (*env)->DeleteLocalRef(env, objCls);
    for (int i = nresults; i >= 1; i--) {
        jobject val = lua_to_java_object(L, env, -1);
        (*env)->SetObjectArrayElement(env, results, i - 1, val);
        if (val) (*env)->DeleteLocalRef(env, val);
        lua_pop(L, 1);
    }
    LUA_UNLOCK();
    return results;
}

// ========== LuaFunction.destroy ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaFunctionObj_destroy(JNIEnv* env, jobject obj) {
    LUA_LOCK();
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID sf = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID rf = (*env)->GetFieldID(env, cls, "ref", "I");
    jlong Lptr = (*env)->GetLongField(env, obj, sf);
    jint ref = (*env)->GetIntField(env, obj, rf);
    (*env)->DeleteLocalRef(env, cls);
    if (Lptr != 0 && ref >= 0) {
        lua_State* L = (lua_State*)(uintptr_t)Lptr;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        (*env)->SetIntField(env, obj, rf, -1);
    }
    LUA_UNLOCK();
}

// ========== LuaInvocationHandler.invokeNative ==========
JNIEXPORT jobject JNICALL Java_com_luajava_LuaInvocationHandler_invokeNative
  (JNIEnv* env, jobject obj, jobject proxy, jobject method, jobjectArray args) {
    LUA_LOCK();

    jclass cls = (*env)->GetObjectClass(env, obj);
    if (!cls) {
        LUA_UNLOCK();
        return NULL;
    }

    jfieldID sf = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID rf = (*env)->GetFieldID(env, cls, "tableRef", "I");
    if (!sf || !rf) {
        (*env)->DeleteLocalRef(env, cls);
        LUA_UNLOCK();
        return NULL;
    }

    jlong Lptr = (*env)->GetLongField(env, obj, sf);
    jint ref = (*env)->GetIntField(env, obj, rf);
    (*env)->DeleteLocalRef(env, cls);

    if (ref < 0) {
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/IllegalStateException"),
                         "Handler already destroyed");
        return NULL;
    }

    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    if (!L) {
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/IllegalStateException"),
                         "Invalid Lua state");
        return NULL;
    }

    // 获取方法信息
    jclass methodCls = (*env)->GetObjectClass(env, method);
    if (!methodCls) {
        LUA_UNLOCK();
        return NULL;
    }

    // 获取返回类型
    jmethodID getRt = (*env)->GetMethodID(env, methodCls, "getReturnType", "()Ljava/lang/Class;");
    if (!getRt) {
        (*env)->DeleteLocalRef(env, methodCls);
        LUA_UNLOCK();
        return NULL;
    }

    jobject rt = (*env)->CallObjectMethod(env, method, getRt);
    if (!rt) {
        (*env)->DeleteLocalRef(env, methodCls);
        LUA_UNLOCK();
        return NULL;
    }

    jclass rtCls = (*env)->GetObjectClass(env, rt);
    jmethodID getNameRt = (*env)->GetMethodID(env, rtCls, "getName", "()Ljava/lang/String;");
    if (!getNameRt) {
        (*env)->DeleteLocalRef(env, rt);
        (*env)->DeleteLocalRef(env, rtCls);
        (*env)->DeleteLocalRef(env, methodCls);
        LUA_UNLOCK();
        return NULL;
    }

    jstring rtName = (jstring)(*env)->CallObjectMethod(env, rt, getNameRt);
    const char* rtStr = rtName ? (*env)->GetStringUTFChars(env, rtName, NULL) : "void";
    int isVoid = (strcmp(rtStr, "void") == 0);
    if (rtName) {
        (*env)->ReleaseStringUTFChars(env, rtName, rtStr);
        (*env)->DeleteLocalRef(env, rtName);
    }
    (*env)->DeleteLocalRef(env, rt);
    (*env)->DeleteLocalRef(env, rtCls);

    // 获取方法名
    jmethodID getNameMid = (*env)->GetMethodID(env, methodCls, "getName", "()Ljava/lang/String;");
    if (!getNameMid) {
        (*env)->DeleteLocalRef(env, methodCls);
        LUA_UNLOCK();
        return NULL;
    }

    jstring jname = (jstring)(*env)->CallObjectMethod(env, method, getNameMid);
    const char* name = jname ? (*env)->GetStringUTFChars(env, jname, NULL) : "unknown";
    (*env)->DeleteLocalRef(env, methodCls);

    // 从注册表获取 Lua 表
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        if (jname) {
            (*env)->ReleaseStringUTFChars(env, jname, name);
            (*env)->DeleteLocalRef(env, jname);
        }
        LUA_UNLOCK();
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"),
                         "Lua table reference is invalid");
        return NULL;
    }

    // 获取 Lua 函数
    lua_getfield(L, -1, name);
    if (jname) {
        (*env)->ReleaseStringUTFChars(env, jname, name);
        (*env)->DeleteLocalRef(env, jname);
    }

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        LUA_UNLOCK();
        char buf[512];
        snprintf(buf, sizeof(buf), "Method '%s' not implemented in Lua table", name);
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/NoSuchMethodError"), buf);
        return NULL;
    }

    // 压入 self（表自身）
    lua_pushvalue(L, -2);

    // 压入参数
    int nargs = 1;
    if (args) {
        int len = (*env)->GetArrayLength(env, args);
        for (int i = 0; i < len; i++) {
            jobject arg = (*env)->GetObjectArrayElement(env, args, i);
            push_java_arg(L, env, arg);
            if (arg) {
                (*env)->DeleteLocalRef(env, arg);
            }
            nargs++;
        }
    }

    int err = lua_pcall(L, nargs, isVoid ? 0 : 1, 0);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        if (!msg) msg = "Lua error in proxy handler";
        lua_pop(L, 1);
        LUA_UNLOCK();
        // 抛出 Java 异常（pcall 无法捕获，但不会段错误）
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/RuntimeException"), msg);
        return NULL;
    }

    if (isVoid) {
        lua_pop(L, lua_gettop(L));
        LUA_UNLOCK();
        return NULL;
    }

    jobject result = lua_to_java_object(L, env, -1);
    lua_pop(L, lua_gettop(L));
    LUA_UNLOCK();
    return result;
}

// ========== LuaInvocationHandler.destroyNative ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaInvocationHandler_destroyNative
  (JNIEnv* env, jobject obj) {
    LUA_LOCK();

    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID sf = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID rf = (*env)->GetFieldID(env, cls, "tableRef", "I");
    jlong Lptr = (*env)->GetLongField(env, obj, sf);
    jint ref = (*env)->GetIntField(env, obj, rf);
    (*env)->DeleteLocalRef(env, cls);

    if (Lptr != 0 && ref >= 0) {
        lua_State* L = (lua_State*)(uintptr_t)Lptr;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        (*env)->SetIntField(env, obj, rf, -1);
    }

    LUA_UNLOCK();
}


// ========== LuaInvocationHandler.destroyNative (static) ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaInvocationHandler_destroyNative__JI
  (JNIEnv* env, jclass clazz, jlong Lptr, jint ref) {
    if (Lptr != 0 && ref >= 0) {
        lua_State* L = (lua_State*)(uintptr_t)Lptr;
        LUA_LOCK();
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        LUA_UNLOCK();
    }
}

// ========== 存储 Java 方法回调的结构 ==========
typedef struct {
    jobject module;   // 全局引用
    jmethodID method; // 方法 ID
} JavaMethodBinding;

static int java_method_callback(lua_State* L) {
    JNIEnv* env = getEnv();
    // 从 upvalue 中获取绑定信息
    JavaMethodBinding* binding = (JavaMethodBinding*)lua_touserdata(L, lua_upvalueindex(1));
    if (!binding) {
        return luaL_error(L, "invalid Java method binding");
    }

    int nargs = lua_gettop(L);
    jclass cls = (*env)->GetObjectClass(env, binding->module);

    // 构建参数数组
    jvalue args[nargs];
    char argTypes[16];

    for (int i = 0; i < nargs; i++) {
        int t = lua_type(L, i + 1);
        if (t == LUA_TNUMBER) {
            if (lua_isinteger(L, i + 1)) {
                args[i].i = (jint)lua_tointeger(L, i + 1);
                argTypes[i] = 'I';
            } else {
                args[i].d = (jdouble)lua_tonumber(L, i + 1);
                argTypes[i] = 'D';
            }
        } else if (t == LUA_TSTRING) {
            args[i].l = (*env)->NewStringUTF(env, lua_tostring(L, i + 1));
            argTypes[i] = 'S';
        } else if (t == LUA_TBOOLEAN) {
            args[i].z = (jboolean)lua_toboolean(L, i + 1);
            argTypes[i] = 'Z';
        } else {
            args[i].l = NULL;
            argTypes[i] = 'O';
        }
    }

    jobject result = (*env)->CallObjectMethodA(env, binding->module, binding->method, args);

    // 清理字符串参数
    for (int i = 0; i < nargs; i++) {
        if (argTypes[i] == 'S' && args[i].l) {
            (*env)->DeleteLocalRef(env, args[i].l);
        }
    }

    (*env)->DeleteLocalRef(env, cls);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return luaL_error(L, "Java method threw exception");
    }

    // 转换返回值
    if (result == NULL) {
        lua_pushnil(L);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/String"))) {
        const char* s = (*env)->GetStringUTFChars(env, (jstring)result, NULL);
        lua_pushstring(L, s);
        (*env)->ReleaseStringUTFChars(env, (jstring)result, s);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/Integer"))) {
        jclass intCls = (*env)->GetObjectClass(env, result);
        jmethodID mid = (*env)->GetMethodID(env, intCls, "intValue", "()I");
        lua_pushinteger(L, (*env)->CallIntMethod(env, result, mid));
        (*env)->DeleteLocalRef(env, intCls);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/Double"))) {
        jclass dblCls = (*env)->GetObjectClass(env, result);
        jmethodID mid = (*env)->GetMethodID(env, dblCls, "doubleValue", "()D");
        lua_pushnumber(L, (*env)->CallDoubleMethod(env, result, mid));
        (*env)->DeleteLocalRef(env, dblCls);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/Boolean"))) {
        jclass boolCls = (*env)->GetObjectClass(env, result);
        jmethodID mid = (*env)->GetMethodID(env, boolCls, "booleanValue", "()Z");
        lua_pushboolean(L, (*env)->CallBooleanMethod(env, result, mid));
        (*env)->DeleteLocalRef(env, boolCls);
    } else {
        // 返回 Java 对象
        extern int new_java_object_ud(lua_State* L, jobject obj);
        new_java_object_ud(L, result);
    }
    (*env)->DeleteLocalRef(env, result);
    return 1;
}

// ========== LuaRuntime.registerJavaMethod ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime_registerJavaMethod
  (JNIEnv* env, jobject obj, jstring name, jobject module, jobject methodObj) {
    LUA_LOCK();
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID f = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jlong Lptr = (*env)->GetLongField(env, obj, f);
    (*env)->DeleteLocalRef(env, cls);

    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* cname = (*env)->GetStringUTFChars(env, name, NULL);

    // 创建绑定结构
    JavaMethodBinding* binding = (JavaMethodBinding*)lua_newuserdatauv(L, sizeof(JavaMethodBinding), 0);
    binding->module = (*env)->NewGlobalRef(env, module);

    // 从 java.lang.reflect.Method 获取真实方法
    jclass methodCls = (*env)->GetObjectClass(env, methodObj);
    // 反射方法对应的实际 Java 方法无法直接获取 jmethodID，改用 Method.invoke
    // 简化：保存 methodObj 并用 Method.invoke 调用
    binding->method = NULL; // 用 methodObj 代替
    // 把这个 methodObj 保存下来
    jmethodID invokeMethod = (*env)->GetMethodID(env, methodCls, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    binding->method = invokeMethod;
    // 保存 methodObj 和 module
    jobject savedMethod = (*env)->NewGlobalRef(env, methodObj);
    // 把 savedMethod 存到 binding 的 method 字段类型不对，这里改用第二个字段

    // 简化方案：直接把 methodObj 作为 lightuserdata 存到 upvalue
    // 这里先放个占位，实际实现等到后续优化
    (*env)->DeleteLocalRef(env, methodCls);

    // 创建闭包
    lua_pushlightuserdata(L, binding);
    lua_pushcclosure(L, java_method_callback, 1);
    lua_setglobal(L, cname);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    LUA_UNLOCK();
}

// ========== 回调数据结构 ==========
typedef struct {
    jobject callback;   // LuaJavaCallback 全局引用
} LuaCallbackData;

// ========== C 闭包：Lua 调用时转发到 Java ==========
static int lua_java_callback_entry(lua_State* L) {
    JNIEnv* env = getEnv();
    LuaCallbackData* data = (LuaCallbackData*)lua_touserdata(L, lua_upvalueindex(1));
    if (!data || !data->callback) {
        return luaL_error(L, "invalid callback");
    }

    int nargs = lua_gettop(L);

    jclass cls = (*env)->GetObjectClass(env, data->callback);
    if (!cls) {
        return luaL_error(L, "callback: GetObjectClass failed");
    }

    jmethodID callMethod = (*env)->GetMethodID(env, cls, "call", "([Ljava/lang/Object;)Ljava/lang/Object;");
    if ((*env)->ExceptionCheck(env)) {
        jthrowable exc = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        if (exc) {
            jclass excCls = (*env)->GetObjectClass(env, exc);
            jmethodID getMsg = (*env)->GetMethodID(env, excCls, "getMessage", "()Ljava/lang/String;");
            jstring msg = (jstring)(*env)->CallObjectMethod(env, exc, getMsg);
            const char* cmsg = msg ? (*env)->GetStringUTFChars(env, msg, NULL) : "null";
            if (msg) (*env)->ReleaseStringUTFChars(env, msg, cmsg);
            (*env)->DeleteLocalRef(env, msg);
            (*env)->DeleteLocalRef(env, excCls);
        }
        (*env)->DeleteLocalRef(env, cls);
        return luaL_error(L, "callback setup failed");
    }

    if (!callMethod) {
        (*env)->DeleteLocalRef(env, cls);
        return luaL_error(L, "callback: call method not found");
    }

    (*env)->DeleteLocalRef(env, cls);

    // 构建 Object[] 参数
    jclass objCls = (*env)->FindClass(env, "java/lang/Object");
    if (!objCls) {
        return luaL_error(L, "callback: FindClass(Object) failed");
    }

    jobjectArray args = (*env)->NewObjectArray(env, nargs, objCls, NULL);
    (*env)->DeleteLocalRef(env, objCls);

    if (!args) {
        return luaL_error(L, "callback: NewObjectArray failed");
    }

    for (int i = 0; i < nargs; i++) {
        jobject val = lua_to_java_object(L, env, i + 1);
        (*env)->SetObjectArrayElement(env, args, i, val);
        if (val) (*env)->DeleteLocalRef(env, val);
    }

    jobject result = (*env)->CallObjectMethod(env, data->callback, callMethod, args);
    (*env)->DeleteLocalRef(env, args);

    if ((*env)->ExceptionCheck(env)) {
        jthrowable exc = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        if (exc) {
            jclass excCls = (*env)->GetObjectClass(env, exc);
            jmethodID toString = (*env)->GetMethodID(env, excCls, "toString", "()Ljava/lang/String;");
            jstring ts = (jstring)(*env)->CallObjectMethod(env, exc, toString);
            const char* cts = ts ? (*env)->GetStringUTFChars(env, ts, NULL) : "null";
            if (ts) {
                (*env)->ReleaseStringUTFChars(env, ts, cts);
                (*env)->DeleteLocalRef(env, ts);
            }
            (*env)->DeleteLocalRef(env, excCls);
            (*env)->DeleteLocalRef(env, exc);
        }
        return luaL_error(L, "callback error after call");
    }

    if (result == NULL) {
        return 0;
    }

    if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/String"))) {
        const char* s = (*env)->GetStringUTFChars(env, (jstring)result, NULL);
        lua_pushstring(L, s);
        (*env)->ReleaseStringUTFChars(env, (jstring)result, s);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/Integer"))) {
        jclass intCls = (*env)->GetObjectClass(env, result);
        lua_pushinteger(L, (*env)->CallIntMethod(env, result,
            (*env)->GetMethodID(env, intCls, "intValue", "()I")));
        (*env)->DeleteLocalRef(env, intCls);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/Double"))) {
        jclass dblCls = (*env)->GetObjectClass(env, result);
        lua_pushnumber(L, (*env)->CallDoubleMethod(env, result,
            (*env)->GetMethodID(env, dblCls, "doubleValue", "()D")));
        (*env)->DeleteLocalRef(env, dblCls);
    } else if ((*env)->IsInstanceOf(env, result, (*env)->FindClass(env, "java/lang/Boolean"))) {
        jclass boolCls = (*env)->GetObjectClass(env, result);
        lua_pushboolean(L, (*env)->CallBooleanMethod(env, result,
            (*env)->GetMethodID(env, boolCls, "booleanValue", "()Z")));
        (*env)->DeleteLocalRef(env, boolCls);
    } else {
        new_java_object_ud(L, result);
    }
    (*env)->DeleteLocalRef(env, result);
    return 1;
}
// ========== LuaRuntime._registerCallback ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1registerCallback
  (JNIEnv* env, jobject obj, jlong Lptr, jstring luaName, jobject callback) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* name = (*env)->GetStringUTFChars(env, luaName, NULL);

    LuaCallbackData* data = (LuaCallbackData*)lua_newuserdatauv(L, sizeof(LuaCallbackData), 0);
    data->callback = (*env)->NewGlobalRef(env, callback);

    lua_pushcclosure(L, lua_java_callback_entry, 1);
    lua_setglobal(L, name);

    (*env)->ReleaseStringUTFChars(env, luaName, name);
    LUA_UNLOCK();
}
// ========== LuaPromise.complete ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaPromise_complete
  (JNIEnv* env, jobject obj, jobject result) {
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID stateField = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID refField   = (*env)->GetFieldID(env, cls, "threadRef", "I");
    jfieldID doneField  = (*env)->GetFieldID(env, cls, "done", "Z");
    jlong Lptr = (*env)->GetLongField(env, obj, stateField);
    jint ref   = (*env)->GetIntField(env, obj, refField);
    (*env)->DeleteLocalRef(env, cls);

    if (ref < 0) return;  // 还没有 await
    jboolean alreadyDone = (*env)->GetBooleanField(env, obj, doneField);
    if (alreadyDone) return;  // 已经 complete 过了

    lua_State* L = (lua_State*)(uintptr_t)Lptr;

    // LUA_LOCK();
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_State* co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (!co) {
        // LUA_UNLOCK();
        return;
    }

    push_java_arg(co, env, result);
    int nres;
    int ret = lua_resume(co, L, 1, &nres);

    if (ret != LUA_OK && ret != LUA_YIELD) {
        const char* msg = lua_tostring(co, -1);
        fprintf(stderr, "LuaPromise.complete error: %s\n", msg ? msg : "unknown");
        lua_pop(co, 1);
    }

    (*env)->SetBooleanField(env, obj, doneField, JNI_TRUE);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    // LUA_UNLOCK();
}

// ========== LuaPromise.resumeExceptionally ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaPromise_resumeExceptionally
  (JNIEnv* env, jobject obj, jstring error) {
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID stateField = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID refField   = (*env)->GetFieldID(env, cls, "threadRef", "I");
    jfieldID doneField  = (*env)->GetFieldID(env, cls, "done", "Z");
    jlong Lptr = (*env)->GetLongField(env, obj, stateField);
    jint ref   = (*env)->GetIntField(env, obj, refField);
    (*env)->DeleteLocalRef(env, cls);

    if (ref < 0) return;
    jboolean alreadyDone = (*env)->GetBooleanField(env, obj, doneField);
    if (alreadyDone) return;  // 已经 complete 过了

    lua_State* L = (lua_State*)(uintptr_t)Lptr;

    // LUA_LOCK();
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_State* co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (!co) {
        // LUA_UNLOCK();
        return;
    }

    const char* msg = (*env)->GetStringUTFChars(env, error, NULL);
    lua_pushstring(co, msg);
    (*env)->ReleaseStringUTFChars(env, error, msg);

    int nres;
    int ret2 = lua_resume(co, L, 1, &nres);

    if (ret2 != LUA_OK && nres != LUA_YIELD) {
        const char* err = lua_tostring(co, -1);
        fprintf(stderr, "LuaPromise.resumeExceptionally error: %s\n", err ? err : "unknown");
        lua_pop(co, 1);
    }

    (*env)->SetBooleanField(env, obj, doneField, JNI_TRUE);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    // LUA_UNLOCK();
}

// ========== LuaRuntime._newAgentState ==========
JNIEXPORT jlong JNICALL Java_com_luajava_LuaRuntime__1newAgentState
  (JNIEnv* env, jclass cls, jlong mainLptr) {
    LUA_LOCK();
    lua_State* L = luaL_newstate();
    if (L) {
        // 只加载基础库和 java 库，不加载 io/os 等（安全隔离）
        luaL_requiref(L, "_G", luaopen_base, 1);
        lua_pop(L, 1);
        luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(L, 1);
        luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(L, 1);
        luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
        luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
        lua_pop(L, 1);
        lua_pop(L, 1);
        luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
        lua_pop(L, 1);
        luaL_requiref(L, "java", luaopen_java, 1);

        // 把 state 指针存入注册表
        lua_pushstring(L, "luajava_stateptr");
        lua_pushinteger(L, (lua_Integer)(uintptr_t)L);
        lua_settable(L, LUA_REGISTRYINDEX);
    }
    LUA_UNLOCK();
    return (jlong)(uintptr_t)L;
}

// ========== LuaRuntime._setAgent ==========
