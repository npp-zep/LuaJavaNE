// jni/luajava.c
#include "luajava.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <lua.h>
extern int luaopen_java(lua_State* L);
extern void lua_open_custom_libs(lua_State* L);
extern void java_promise_cleanup_state(lua_State* L);
#include "com_luajava_LuaRuntime.h"
#include "com_luajava_LuaFunctionObj.h"
#include "com_luajava_LuaInvocationHandler.h"
#include "com_luajava_LuaTable.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "../jni_compat.h"


JavaVM* g_jvm = NULL;
// 在 luajava.c 顶部，将静态初始化改为声明
pthread_mutex_t lua_mutex;   // 不再静态初始化



#define LUA_LOCK()   pthread_mutex_lock(&lua_mutex)
#define LUA_UNLOCK() pthread_mutex_unlock(&lua_mutex)

// ========== lua_State 引用计数 ==========
// 每个 lua_State 由 LuaRuntime 持有 1 个引用；
// 每次创建 LuaFunctionObj / LuaInvocationHandler（会在注册表占用一个 ref）时再 +1。
// 引用计数归零时才真正 lua_close，避免"先关 LuaRuntime、后销毁函数对象"导致的 use-after-free。
typedef struct LuaStateEntry {
    lua_State* L;
    int refs;
    struct LuaStateEntry* next;
} LuaStateEntry;

static LuaStateEntry* g_state_list = NULL;

static LuaStateEntry* lua_state_find(lua_State* L) {
    for (LuaStateEntry* e = g_state_list; e; e = e->next)
        if (e->L == L) return e;
    return NULL;
}

void lua_state_add_ref(lua_State* L) {
    if (!L) return;
    LUA_LOCK();
    LuaStateEntry* e = lua_state_find(L);
    if (e) {
        e->refs++;
    } else {
        e = (LuaStateEntry*)malloc(sizeof(LuaStateEntry));
        e->L = L;
        e->refs = 1;
        e->next = g_state_list;
        g_state_list = e;
    }
    LUA_UNLOCK();
}

// 释放一个引用；返回 1 表示该状态应被真正 lua_close
int lua_state_release_ref(lua_State* L) {
    if (!L) return 0;
    LUA_LOCK();
    LuaStateEntry* e = lua_state_find(L);
    if (!e) {
        // 未被跟踪的状态：保守起见交给调用方关闭
        LUA_UNLOCK();
        return 1;
    }
    if (--e->refs > 0) {
        LUA_UNLOCK();
        return 0;
    }
    // 从链表移除并释放条目
    if (g_state_list == e) {
        g_state_list = e->next;
    } else {
        LuaStateEntry* prev = g_state_list;
        while (prev && prev->next != e) prev = prev->next;
        if (prev) prev->next = e->next;
    }
    free(e);
    LUA_UNLOCK();
    return 1;
}

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
            JNI_ATTACH(g_jvm, env);
        }
    }
    return env;
}

// 抛出指定类名的 Java 异常（统一入口，简化错误处理）。
// 若目标异常类不在 classpath，则回退到 RuntimeException，保证错误必达。
void throwJavaException(JNIEnv* env, const char* className, const char* msg) {
    if (!env || !msg) return;
    jclass cls = (*env)->FindClass(env, className);
    if (!cls) {
        (*env)->ExceptionClear(env);
        cls = (*env)->FindClass(env, "java/lang/RuntimeException");
        if (!cls) return;
    }
    (*env)->ThrowNew(env, cls, msg);
    (*env)->DeleteLocalRef(env, cls);
}

// 先对 Lua 错误码分类，再抛对应异常：
//   LUA_ERRSYNTAX     -> LuaSyntaxError（编译期/load 错误）
//   其他             -> LuaRuntimeError（执行期 pcall 错误）
void throwLuaError(JNIEnv* env, lua_State* L, int errCode) {
    if (errCode != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        const char* ex = (errCode == LUA_ERRSYNTAX)
            ? "com/luajava/exception/LuaSyntaxError"
            : "com/luajava/exception/LuaRuntimeError";
        throwJavaException(env, ex, msg ? msg : "Unknown Lua error");
    }
}

// ========== Lua ↔ Java 类型转换 ==========

// 将 LuaFunctionObj（持 Lua 函数注册表引用）还原压入调用方 Lua 栈，返回 1。
// 需在已持有 LUA_LOCK 的上下文内调用；本进程为单 lua_State，故按调用方 state rawgeti 即可。
int push_luafunc_to_lua(lua_State* L, JNIEnv* env, jobject obj) {
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID sf = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID rf = (*env)->GetFieldID(env, cls, "ref", "I");
    jint ref = (*env)->GetIntField(env, obj, rf);
    (*env)->DeleteLocalRef(env, cls);
    if (ref >= 0) lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    else lua_pushnil(L);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); lua_pushnil(L); }
    return 1;
}

void push_java_arg(lua_State* L, JNIEnv* env, jobject arg) {
    if (arg == NULL) {
        lua_pushnil(L);
        return;
    }

    // LuaFunctionObj -> 还原为原 Lua 函数（放回 Lua table / 容器时保持可调用）
    jclass fnCls = (*env)->FindClass(env, "com/luajava/LuaFunctionObj");
    if ((*env)->IsInstanceOf(env, arg, fnCls)) {
        (*env)->DeleteLocalRef(env, fnCls);
        push_luafunc_to_lua(L, env, arg);
        return;
    }
    (*env)->DeleteLocalRef(env, fnCls);

    // String -> Lua 字符串
    jclass cls = (*env)->FindClass(env, "java/lang/String");
    if ((*env)->IsInstanceOf(env, arg, cls)) {
        (*env)->DeleteLocalRef(env, cls);
        const char* str = (*env)->GetStringUTFChars(env, (jstring)arg, NULL);
        lua_pushstring(L, str);
        (*env)->ReleaseStringUTFChars(env, (jstring)arg, str);
        return;
    }
    (*env)->DeleteLocalRef(env, cls);

    // Boolean -> Lua 布尔
    cls = (*env)->FindClass(env, "java/lang/Boolean");
    if ((*env)->IsInstanceOf(env, arg, cls)) {
        (*env)->DeleteLocalRef(env, cls);
        cls = (*env)->GetObjectClass(env, arg);
        jmethodID mid = (*env)->GetMethodID(env, cls, "booleanValue", "()Z");
        lua_pushboolean(L, (*env)->CallBooleanMethod(env, arg, mid));
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    (*env)->DeleteLocalRef(env, cls);

    // Character -> Lua 单字符字符串
    cls = (*env)->FindClass(env, "java/lang/Character");
    if ((*env)->IsInstanceOf(env, arg, cls)) {
        (*env)->DeleteLocalRef(env, cls);
        cls = (*env)->GetObjectClass(env, arg);
        jmethodID mid = (*env)->GetMethodID(env, cls, "charValue", "()C");
        jchar ch = (*env)->CallCharMethod(env, arg, mid);
        char buf[1] = { (char)ch };
        lua_pushlstring(L, buf, 1);
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    (*env)->DeleteLocalRef(env, cls);

    // Number：整数类 -> Lua 整数，浮点类 -> Lua 浮点
    cls = (*env)->FindClass(env, "java/lang/Number");
    if ((*env)->IsInstanceOf(env, arg, cls)) {
        (*env)->DeleteLocalRef(env, cls);
        cls = (*env)->GetObjectClass(env, arg);
        jclass intCls   = (*env)->FindClass(env, "java/lang/Integer");
        jclass longCls  = (*env)->FindClass(env, "java/lang/Long");
        jclass shortCls = (*env)->FindClass(env, "java/lang/Short");
        jclass byteCls  = (*env)->FindClass(env, "java/lang/Byte");
        jclass dblCls   = (*env)->FindClass(env, "java/lang/Double");
        jclass fltCls   = (*env)->FindClass(env, "java/lang/Float");
        if ((*env)->IsInstanceOf(env, arg, intCls) ||
            (*env)->IsInstanceOf(env, arg, longCls) ||
            (*env)->IsInstanceOf(env, arg, shortCls) ||
            (*env)->IsInstanceOf(env, arg, byteCls)) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "longValue", "()J");
            lua_pushinteger(L, (lua_Integer)(*env)->CallLongMethod(env, arg, mid));
        } else {
            jmethodID mid = (*env)->GetMethodID(env, cls, "doubleValue", "()D");
            lua_pushnumber(L, (lua_Number)(*env)->CallDoubleMethod(env, arg, mid));
        }
        (*env)->DeleteLocalRef(env, fltCls);
        (*env)->DeleteLocalRef(env, dblCls);
        (*env)->DeleteLocalRef(env, byteCls);
        (*env)->DeleteLocalRef(env, shortCls);
        (*env)->DeleteLocalRef(env, longCls);
        (*env)->DeleteLocalRef(env, intCls);
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    (*env)->DeleteLocalRef(env, cls);

    // 其他对象（含数组）：包装为 userdata（数组自动使用 Java.Array 语义）；
    // Map/List 经 java_table_dispatch 包成惰性 JavaTable
    extern int java_table_dispatch(lua_State* L, jobject obj);
    java_table_dispatch(L, arg);
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
            lua_state_add_ref(L); // 该 LuaFunctionObj 占用状态的一个引用
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
        case LUA_TTABLE: {
            lua_pushvalue(L, idx);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_state_add_ref(L); // 该 LuaTable 占用状态的一个引用
            lua_pushstring(L, "luajava_stateptr");
            lua_rawget(L, LUA_REGISTRYINDEX);
            jlong statePtr = (jlong)lua_tointeger(L, -1);
            lua_pop(L, 1);
            jclass cls = (*env)->FindClass(env, "com/luajava/LuaTable");
            jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(JI)V");
            jobject obj = (*env)->NewObject(env, cls, ctor, statePtr, (jint)ref);
            (*env)->DeleteLocalRef(env, cls);
            return obj;
        }
        case LUA_TUSERDATA:
            // Java.Object / Java.Class / Java.Array / Java.Table → 底层对象
            return java_ud_to_object(L, idx);
        default: return NULL;
    }
}

// ========== 从 LuaTable Java 对象取出 statePtr/ref 字段 ==========
static int luatable_get_refs(JNIEnv* env, jobject obj, lua_State** Lout, int* refout) {
    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID sf = (*env)->GetFieldID(env, cls, "statePtr", "J");
    jfieldID rf = (*env)->GetFieldID(env, cls, "ref", "I");
    jlong Lptr = (*env)->GetLongField(env, obj, sf);
    jint ref = (*env)->GetIntField(env, obj, rf);
    (*env)->DeleteLocalRef(env, cls);
    if (Lptr == 0 || ref < 0) return 0;
    *Lout = (lua_State*)(uintptr_t)Lptr;
    *refout = ref;
    return 1;
}

// ========== LuaTable.getNative ==========
JNIEXPORT jobject JNICALL Java_com_luajava_LuaTable_getNative
  (JNIEnv* env, jobject obj, jobject key) {
    LUA_LOCK();
    lua_State* L; int ref;
    jobject result = NULL;
    if (luatable_get_refs(env, obj, &L, &ref)) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);   // push table
        if (key == NULL) lua_pushnil(L);
        else push_java_arg(L, env, key);          // push key
        lua_rawget(L, -2);                        // table[key]
        result = lua_to_java_object(L, env, -1);
        lua_pop(L, 2);
    }
    LUA_UNLOCK();
    return result;
}

// ========== LuaTable.putNative ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaTable_putNative
  (JNIEnv* env, jobject obj, jobject key, jobject value) {
    LUA_LOCK();
    lua_State* L; int ref;
    if (luatable_get_refs(env, obj, &L, &ref)) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);   // push table
        if (key == NULL) lua_pushnil(L);
        else push_java_arg(L, env, key);          // push key
        if (value == NULL) lua_pushnil(L);
        else push_java_arg(L, env, value);        // push value
        lua_rawset(L, -3);                        // table[key] = value
        lua_pop(L, 1);
    }
    LUA_UNLOCK();
}

// ========== LuaTable.sizeNative ==========
JNIEXPORT jint JNICALL Java_com_luajava_LuaTable_sizeNative
  (JNIEnv* env, jobject obj) {
    LUA_LOCK();
    lua_State* L; int ref;
    jint size = 0;
    if (luatable_get_refs(env, obj, &L, &ref)) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        size = (jint)lua_rawlen(L, -1);
        lua_pop(L, 1);
    }
    LUA_UNLOCK();
    return size;
}

// ========== LuaTable.keysNative ==========
JNIEXPORT jobjectArray JNICALL Java_com_luajava_LuaTable_keysNative
  (JNIEnv* env, jobject obj) {
    LUA_LOCK();
    jclass objCls = (*env)->FindClass(env, "java/lang/Object");
    jobjectArray arr = (*env)->NewObjectArray(env, 0, objCls, NULL);
    lua_State* L; int ref;
    if (luatable_get_refs(env, obj, &L, &ref)) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        // 第一遍统计键个数
        int count = 0;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) { lua_pop(L, 1); count++; }
        (*env)->DeleteLocalRef(env, objCls);
        objCls = (*env)->FindClass(env, "java/lang/Object");
        jobjectArray out = (*env)->NewObjectArray(env, count, objCls, NULL);
        // 第二遍收集键
        int i = 0;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            jobject k = lua_to_java_object(L, env, -2);   // key 在 -2
            (*env)->SetObjectArrayElement(env, out, i++, k);
            if (k) (*env)->DeleteLocalRef(env, k);
            lua_pop(L, 1);                                 // 弹出 value，保留 key
        }
        lua_pop(L, 1);                                     // 弹出 table
        arr = out;
    }
    (*env)->DeleteLocalRef(env, objCls);
    LUA_UNLOCK();
    return arr;
}

// ========== LuaTable.destroyNative ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaTable_destroyNative
  (JNIEnv* env, jobject obj) {
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
        if (lua_state_release_ref(L)) {
            java_promise_cleanup_state(L);
            lua_close(L);
        }
    }
    LUA_UNLOCK();
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
        lua_state_add_ref(L); // LuaRuntime 持有 1 个引用
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
    int err = luaL_loadstring(L, code);
    (*env)->ReleaseStringUTFChars(env, script, code);
    if (err == LUA_OK) err = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        const char* ex = (err == LUA_ERRSYNTAX)
            ? "com/luajava/exception/LuaSyntaxError"
            : "com/luajava/exception/LuaRuntimeError";
        throwJavaException(env, ex, msg ? msg : "Unknown Lua error");
        return;
    }
    LUA_UNLOCK();
}

// ========== LuaRuntime._close ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1close(JNIEnv* env, jobject obj, jlong Lptr) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    // 释放 LuaRuntime 持有的引用；若仍有 LuaFunctionObj/LuaInvocationHandler 引用，
    // 暂不关闭，等最后一个引用释放时才真正 lua_close，避免 use-after-free。
    if (L && lua_state_release_ref(L)) {
        // 先清理该状态在 Promise 注册表中的回调引用，再关闭状态
        java_promise_cleanup_state(L);
        lua_close(L);
    }
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
        throwJavaException(env, "com/luajava/exception/LuaSyntaxError",
                           msg ? msg : "compile error");
        return -1;
    }
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_state_add_ref(L); // 该 LuaFunctionObj 占用状态的一个引用
    LUA_UNLOCK();
    return ref;
}

// ========== LuaRuntime._doFile ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1doFile(JNIEnv* env, jobject obj,
                                                             jlong Lptr, jstring path) {
    LUA_LOCK();
    lua_State* L = (lua_State*)(uintptr_t)Lptr;
    const char* cpath = (*env)->GetStringUTFChars(env, path, NULL);
    int err = luaL_loadfile(L, cpath);
    (*env)->ReleaseStringUTFChars(env, path, cpath);
    if (err == LUA_OK) err = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (err != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        LUA_UNLOCK();
        const char* ex = (err == LUA_ERRSYNTAX)
            ? "com/luajava/exception/LuaSyntaxError"
            : "com/luajava/exception/LuaRuntimeError";
        throwJavaException(env, ex, msg ? msg : "Unknown Lua error");
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
        throwJavaException(env, "com/luajava/exception/LuaRuntimeError", "not a function");
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
        throwJavaException(env, "com/luajava/exception/LuaRuntimeError",
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
JNIEXPORT jobjectArray JNICALL Java_com_luajava_LuaFunctionObj_callMultipleNative
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
        throwJavaException(env, "com/luajava/exception/LuaRuntimeError", "invalid function ref");
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
        throwJavaException(env, "com/luajava/exception/LuaRuntimeError",
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
JNIEXPORT void JNICALL Java_com_luajava_LuaFunctionObj_destroyNative(JNIEnv* env, jobject obj) {
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
        // 释放该 LuaFunctionObj 对状态持有的引用；若为最后一个，则真正关闭状态
        if (lua_state_release_ref(L)) {
            java_promise_cleanup_state(L);
            lua_close(L);
        }
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
        throwJavaException(env, "com/luajava/exception/LuaRuntimeError",
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
        throwJavaException(env, "com/luajava/exception/LuaRuntimeError", msg);
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

// ========== LuaInvocationHandler.destroyNative (static) ==========
JNIEXPORT void JNICALL Java_com_luajava_LuaInvocationHandler_destroyNative__JI
  (JNIEnv* env, jclass clazz, jlong Lptr, jint ref) {
    if (Lptr != 0 && ref >= 0) {
        lua_State* L = (lua_State*)(uintptr_t)Lptr;
        LUA_LOCK();
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        // 释放该 handler 对状态持有的引用；若为最后一个，则真正关闭状态
        if (lua_state_release_ref(L)) {
            java_promise_cleanup_state(L);
            lua_close(L);
        }
        LUA_UNLOCK();
    }
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
        extern int java_table_dispatch(lua_State* L, jobject obj);
        java_table_dispatch(L, result);
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
// ========== LuaRuntime._setAgent ==========
