#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;
extern int new_java_object_ud(lua_State* L, jobject obj);

static pthread_mutex_t promise_mutex = PTHREAD_MUTEX_INITIALIZER;

JNIEXPORT void JNICALL Java_com_luajava_LuaAgent_complete
  (JNIEnv* env, jclass cls, jint pid, jstring result) {
    const char* s = (*env)->GetStringUTFChars(env, result, NULL);
    
    pthread_mutex_lock(&promise_mutex);
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
    pthread_mutex_unlock(&promise_mutex);
    
    (*env)->ReleaseStringUTFChars(env, result, s);
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
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
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
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
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
    int initial = lua_gettop(L);
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_registry;
    while (e) {
        if (e->id == id) {
            lua_pushboolean(L, e->done);
            if (e->done && e->result) {
                char* r = e->result;
                switch (r[0]) {
                    case 'M': {
                        int count = atoi(r + 1);
                        char* p = strchr(r, '|');
                        if (!p) { lua_pushnil(L); break; }
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
                            p = end;
                            if (p) p++;
                        }
                        break;
                    }
                    case 'S': lua_pushstring(L, r + 2); break;
                    case 'I': lua_pushinteger(L, atoll(r + 2)); break;
                    case 'N': lua_pushnumber(L, atof(r + 2)); break;
                    case 'B': lua_pushboolean(L, (r[2] == 't' || r[2] == '1')); break;
                    case 'O': lua_pushinteger(L, atoi(r + 2)); break;
                    case 'E': lua_pushstring(L, r + 2); break;
                    default:  lua_pushnil(L); break;
                }
            } else {
                lua_pushnil(L);
            }
            pthread_mutex_unlock(&promise_mutex);
            return lua_gettop(L) - initial;  // 返回压入栈的总数（含 done）
        }
        e = e->next;
    }
    pthread_mutex_unlock(&promise_mutex);
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return 2;
}

int java_getObject(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    JNIEnv* env = NULL;
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
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
