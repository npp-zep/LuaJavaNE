#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../jni_compat.h"

extern JavaVM* g_jvm;
extern int new_java_object_ud(lua_State* L, jobject obj);

pthread_mutex_t promise_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========== 共享解析器：把序列化结果串按前缀压入栈 ==========
// 返回压入栈的值的个数；供 checkPromise 与回调派发共用，保证结果语义一致
static int push_parsed_result(lua_State* L, const char* r) {
    int pushed = 0;
    if (!r || !r[0]) return pushed;
    switch (r[0]) {
        case 'M': {
            int count = atoi(r + 1);
            char* p = strchr(r, '|');
            if (!p) { lua_pushnil(L); pushed = 1; break; }
            p++;
            for (int i = 0; i < count && p && *p; i++) {
                char type = *p;
                char* val = p + 2;
                char* end = strchr(p, '|');
                size_t len = end ? (size_t)(end - val) : strlen(val);
                switch (type) {
                    case 'S': lua_pushlstring(L, val, len); break;
                    case 'I': { char buf[32]; memcpy(buf, val, len); buf[len]='\0'; lua_pushinteger(L, atoll(buf)); break; }
                    case 'N': { char buf[64]; memcpy(buf, val, len); buf[len]='\0'; lua_pushnumber(L, atof(buf)); break; }
                    case 'B': lua_pushboolean(L, (len == 4 && memcmp(val, "true", 4)==0) || (len == 1 && *val == '1')); break;
                    case 'X': lua_pushnil(L); break;
                    default: lua_pushnil(L);
                }
                pushed++;
                p = end;
                if (p) p++;
            }
            break;
        }
        case 'S': lua_pushstring(L, r + 2); pushed = 1; break;
        case 'I': lua_pushinteger(L, atoll(r + 2)); pushed = 1; break;
        case 'N': lua_pushnumber(L, atof(r + 2)); pushed = 1; break;
        case 'B': lua_pushboolean(L, (r[2] == 't' || r[2] == '1')); pushed = 1; break;
        case 'O': lua_pushinteger(L, atoi(r + 2)); pushed = 1; break;
        case 'E': lua_pushstring(L, r + 2); pushed = 1; break;
        default:  lua_pushnil(L); pushed = 1; break;
    }
    return pushed;
}

// ========== 回调派发（在工作线程上执行） ==========
// 锁序：只持有 lua_mutex（递归锁），不再持有 promise_mutex，避免与主线程死锁。
// 调用方必须先拷贝出 cbRef/owner/result 并释放 promise_mutex。
void dispatch_callback(lua_State* owner, int cbRef, const char* result) {
    if (cbRef == LUA_NOREF || !owner) return;
    pthread_mutex_lock(&lua_mutex);
    int base = lua_gettop(owner);   // 记录进入时的栈底，防止把调用方遗留的栈值误当参数
    lua_rawgeti(owner, LUA_REGISTRYINDEX, cbRef);
    if (lua_isfunction(owner, -1)) {
        // 参数约定：callback(err, result...)；err 为 E: 错误串，成功时为 nil
        if (result && result[0] == 'E') {
            lua_pushstring(owner, result + 2);
        } else {
            lua_pushnil(owner);
            if (result && result[0]) push_parsed_result(owner, result);
            else lua_pushnil(owner);
        }
        int nargs = lua_gettop(owner) - (base + 1);   // 函数之上的参数个数
        if (lua_pcall(owner, nargs, 0, 0) != LUA_OK) {
            const char* msg = lua_tostring(owner, -1);
            fprintf(stderr, "java.onComplete callback error: %s\n", msg ? msg : "unknown");
            lua_pop(owner, 1);
        }
        luaL_unref(owner, LUA_REGISTRYINDEX, cbRef);
    } else {
        lua_pop(owner, 1);
    }
    pthread_mutex_unlock(&lua_mutex);
}

JNIEXPORT void JNICALL Java_com_luajava_LuaAgent_complete
  (JNIEnv* env, jclass cls, jint pid, jstring result) {
    const char* s = (*env)->GetStringUTFChars(env, result, NULL);

    lua_State* owner = NULL;
    int cbRef = LUA_NOREF;
    char* copy = NULL;

    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_find(pid);
    if (e) {
        if (e->result) free(e->result);
        e->result = strdup(s);
        e->done = 1;
        if (e->callbackRef != LUA_NOREF) {
            owner = e->owner;
            cbRef = e->callbackRef;
            copy = strdup(s);
            e->callbackRef = LUA_NOREF;
            promise_remove(e);
        }
    }
    pthread_mutex_unlock(&promise_mutex);

    // 先释放 promise_mutex 再进 Lua，锁序：promise_mutex → 释放 → lua_mutex
    if (copy) {
        dispatch_callback(owner, cbRef, copy);
        free(copy);
    }
    (*env)->ReleaseStringUTFChars(env, result, s);
}

// 状态关闭时清理该状态注册的回调引用，避免悬空
void java_promise_cleanup_state(lua_State* L) {
    if (!L) return;
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry** pp = &promise_registry;
    while (*pp) {
        PromiseEntry* e = *pp;
        if (e->owner == L) {
            if (e->callbackRef != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, e->callbackRef);
                e->callbackRef = LUA_NOREF;
            }
            *pp = e->next;
            if (e->result) free(e->result);
            free(e);
        } else {
            pp = &e->next;
        }
    }
    pthread_mutex_unlock(&promise_mutex);
}

static char get_type_hint(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER: return lua_isinteger(L, idx) ? 'I' : 'D';
        case LUA_TSTRING: return 'S';
        case LUA_TBOOLEAN: return 'Z';
        default: return 'S';
    }
}

int java_runAsync(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    const char* mtd = luaL_checkstring(L, 3);

    int argCount = lua_gettop(L) - 3;
    int pairCount = argCount * 2;

    JNIEnv* env = NULL;
    JNI_ATTACH(g_jvm, env);
    if (!env) return 0;

    jclass taskCls = (*env)->FindClass(env, "com/luajava/AgentTask");
    jmethodID ctor = (*env)->GetMethodID(env, taskCls, "<init>", "(ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V");
    jstring jcls = (*env)->NewStringUTF(env, cls);
    jstring jmtd = (*env)->NewStringUTF(env, mtd);

    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray jargs = (*env)->NewObjectArray(env, pairCount > 0 ? pairCount : 0, strCls, NULL);
    for (int i = 0; i < argCount; i++) {
        const char* val = lua_tostring(L, 4 + i);
        char hint[2] = { get_type_hint(L, 4 + i), '\0' };
        jstring jval = (*env)->NewStringUTF(env, val ? val : "");
        jstring jhint = (*env)->NewStringUTF(env, hint);
        (*env)->SetObjectArrayElement(env, jargs, i * 2, jval);
        (*env)->SetObjectArrayElement(env, jargs, i * 2 + 1, jhint);
        (*env)->DeleteLocalRef(env, jval);
        (*env)->DeleteLocalRef(env, jhint);
    }

    jobject task = (*env)->NewObject(env, taskCls, ctor, (jint)pid, jcls, jmtd, jargs);
    (*env)->DeleteLocalRef(env, jcls);
    (*env)->DeleteLocalRef(env, jmtd);
    (*env)->DeleteLocalRef(env, jargs);
    (*env)->DeleteLocalRef(env, taskCls);

    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID submitMid = (*env)->GetStaticMethodID(env, agentCls, "submitTask", "(Lcom/luajava/AgentTask;)V");
    if (submitMid) {
        (*env)->CallStaticVoidMethod(env, agentCls, submitMid, task);
    }
    (*env)->DeleteLocalRef(env, task);
    (*env)->DeleteLocalRef(env, agentCls);

    return 0;
}

int java_runAsyncObj(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TUSERDATA);
    const char* method = luaL_checkstring(L, 3);
    int argCount = lua_gettop(L) - 3;
    int pairCount = argCount * 2;

    JNIEnv* env = NULL;
    JNI_ATTACH(g_jvm, env);
    if (!env) return 0;

    jclass taskCls = (*env)->FindClass(env, "com/luajava/AgentTask");
    jmethodID ctor = (*env)->GetMethodID(env, taskCls, "<init>", "(ILjava/lang/Object;Ljava/lang/String;[Ljava/lang/String;)V");
    jstring jmtd = (*env)->NewStringUTF(env, method);

    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray jargs = (*env)->NewObjectArray(env, pairCount > 0 ? pairCount : 0, strCls, NULL);
    for (int i = 0; i < argCount; i++) {
        const char* val = lua_tostring(L, 4 + i);
        char hint[2] = { get_type_hint(L, 4 + i), '\0' };
        jstring jval = (*env)->NewStringUTF(env, val ? val : "");
        jstring jhint = (*env)->NewStringUTF(env, hint);
        (*env)->SetObjectArrayElement(env, jargs, i * 2, jval);
        (*env)->SetObjectArrayElement(env, jargs, i * 2 + 1, jhint);
        (*env)->DeleteLocalRef(env, jval);
        (*env)->DeleteLocalRef(env, jhint);
    }

    jobject jobj = java_get_obj(L, 2);
    jobject task = (*env)->NewObject(env, taskCls, ctor, (jint)pid, jobj, jmtd, jargs);
    (*env)->DeleteLocalRef(env, jmtd);
    (*env)->DeleteLocalRef(env, jargs);
    (*env)->DeleteLocalRef(env, taskCls);

    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID submitMid = (*env)->GetStaticMethodID(env, agentCls, "submitTask", "(Lcom/luajava/AgentTask;)V");
    if (submitMid) (*env)->CallStaticVoidMethod(env, agentCls, submitMid, task);
    (*env)->DeleteLocalRef(env, task);
    (*env)->DeleteLocalRef(env, agentCls);
    return 0;
}

int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_find(id);
    if (!e) {
        pthread_mutex_unlock(&promise_mutex);
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        return 2;
    }
    lua_pushboolean(L, e->done);
    int n = 1;
    if (e->done && e->result) {
        int pushed = push_parsed_result(L, e->result);
        n += pushed;
        // 已消费 + 无回调 → 清理条目（修复泄漏）
        if (e->callbackRef == LUA_NOREF) {
            promise_remove(e);
        }
    } else {
        lua_pushnil(L);
        n++;
    }
    pthread_mutex_unlock(&promise_mutex);
    return n;
}

int java_getObject(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    JNIEnv* env = NULL;
    JNI_ATTACH(g_jvm, env);
    if (!env) { lua_pushnil(L); return 1; }

    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID getObjMid = (*env)->GetStaticMethodID(env, agentCls, "getObject", "(I)Ljava/lang/Object;");
    if (!getObjMid) { (*env)->ExceptionClear(env); lua_pushnil(L); (*env)->DeleteLocalRef(env, agentCls); return 1; }

    jobject obj = (*env)->CallStaticObjectMethod(env, agentCls, getObjMid, (jint)id);
    (*env)->DeleteLocalRef(env, agentCls);
    if (!obj) { lua_pushnil(L); return 1; }

    new_java_object_ud(L, obj);  // 使用 lualibjava.c 中的函数包装
    (*env)->DeleteLocalRef(env, obj);
    return 1;
}
