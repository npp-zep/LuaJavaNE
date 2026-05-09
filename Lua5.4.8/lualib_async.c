// Lua5.4.8/lualib_async.c — 线程池异步（稳定大规模）
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;

// LuaAgent.complete() 的 JNI 实现
JNIEXPORT void JNICALL Java_com_luajava_LuaAgent_complete
  (JNIEnv* env, jclass cls, jint pid, jstring result) {
    const char* s = (*env)->GetStringUTFChars(env, result, NULL);
    
    PromiseEntry* e = promise_registry;
    while (e) {
        if (e->id == pid) {
            if (e->result) free(e->result);
            e->result = strdup(s);
            e->done = 1;
            break;
        }
        e = e->next;
    }
    
    (*env)->ReleaseStringUTFChars(env, result, s);
}

int java_runAsync(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    const char* mtd = luaL_checkstring(L, 3);
    
    int argCount = lua_gettop(L) - 3;
    
    JNIEnv* env = NULL;
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
    if (!env) return 0;
    
    // 构建 AgentTask
    jclass taskCls = (*env)->FindClass(env, "com/luajava/AgentTask");
    jmethodID ctor = (*env)->GetMethodID(env, taskCls, "<init>", "(ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V");
    
    jstring jcls = (*env)->NewStringUTF(env, cls);
    jstring jmtd = (*env)->NewStringUTF(env, mtd);
    
    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray jargs = (*env)->NewObjectArray(env, argCount, strCls, NULL);
    for (int i = 0; i < argCount; i++) {
        const char* a = lua_tostring(L, 4 + i);
        jstring ja = (*env)->NewStringUTF(env, a ? a : "");
        (*env)->SetObjectArrayElement(env, jargs, i, ja);
        (*env)->DeleteLocalRef(env, ja);
    }
    
    jobject task = (*env)->NewObject(env, taskCls, ctor, (jint)pid, jcls, jmtd, jargs);
    (*env)->DeleteLocalRef(env, jcls);
    (*env)->DeleteLocalRef(env, jmtd);
    (*env)->DeleteLocalRef(env, jargs);
    (*env)->DeleteLocalRef(env, taskCls);
    
    // 提交到 Agent
    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID submitMid = (*env)->GetStaticMethodID(env, agentCls, "submitTask", "(Lcom/luajava/AgentTask;)V");
    if (submitMid) {
        (*env)->CallStaticVoidMethod(env, agentCls, submitMid, task);
    }
    (*env)->DeleteLocalRef(env, task);
    (*env)->DeleteLocalRef(env, agentCls);
    
    return 0;
}

int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    PromiseEntry* e = promise_registry;
    while (e) {
        if (e->id == id) {
            lua_pushboolean(L, e->done);
            if (e->done && e->result) {
                char* r = e->result;
                switch (r[0]) {
                    case 'S': lua_pushstring(L, r + 2); break;
                    case 'I': lua_pushinteger(L, atoll(r + 2)); break;
                    case 'N': lua_pushnumber(L, atof(r + 2)); break;
                    case 'B': lua_pushboolean(L, r[2] == '1'); break;
                    case 'E': lua_pushstring(L, r + 2); lua_error(L); break;
                    default:  lua_pushnil(L); break;
                }
            } else {
                lua_pushnil(L);
            }
            return 2;
        }
        e = e->next;
    }
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return 2;
}
