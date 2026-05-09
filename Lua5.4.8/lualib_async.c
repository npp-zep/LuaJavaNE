// Lua5.4.8/lualib_async.c — Agent 异步：工作线程调 Java，Promise 回传
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;

typedef struct {
    int pid;
    char* className;
    char* methodName;
    int argCount;
    char** args;
} Task;

static void complete(int pid, const char* s) {
    PromiseEntry* e = promise_registry;
    while (e) {
        if (e->id == pid) {
            if (e->result) free(e->result);
            e->result = strdup(s);
            e->done = 1;
            return;
        }
        e = e->next;
    }
}

static jobjectArray build_string_array(JNIEnv* env, char** strs, int count) {
    jclass stringCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray arr = (*env)->NewObjectArray(env, count, stringCls, NULL);
    for (int i = 0; i < count; i++) {
        jstring js = (*env)->NewStringUTF(env, strs[i] ? strs[i] : "");
        (*env)->SetObjectArrayElement(env, arr, i, js);
        (*env)->DeleteLocalRef(env, js);
    }
    return arr;
}

static void* worker(void* arg) {
    Task* t = (Task*)arg;
    JNIEnv* env = NULL;
    if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != 0) {
        complete(t->pid, "E:attach failed");
        goto cleanup;
    }

    char result[1024] = "X:";
    jclass runnerCls = (*env)->FindClass(env, "com/luajava/AsyncRunner");
    if (!runnerCls) {
        snprintf(result, sizeof(result), "E:AsyncRunner not found");
        goto done;
    }

    jmethodID runMid = (*env)->GetStaticMethodID(env, runnerCls, "runStatic",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)Ljava/lang/String;");
    if (!runMid) {
        snprintf(result, sizeof(result), "E:method not found: runStatic");
        (*env)->DeleteLocalRef(env, runnerCls);
        goto done;
    }

    jstring jcls = (*env)->NewStringUTF(env, t->className);
    jstring jmethod = (*env)->NewStringUTF(env, t->methodName);
    jobjectArray jargs = build_string_array(env, t->args, t->argCount);

    jstring jres = (jstring)(*env)->CallStaticObjectMethod(env, runnerCls, runMid, jcls, jmethod, jargs);
    if (jres) {
        const char* s = (*env)->GetStringUTFChars(env, jres, NULL);
        snprintf(result, sizeof(result), "S:%s", s);
        (*env)->ReleaseStringUTFChars(env, jres, s);
        (*env)->DeleteLocalRef(env, jres);
    }

    (*env)->DeleteLocalRef(env, jcls);
    (*env)->DeleteLocalRef(env, jmethod);
    (*env)->DeleteLocalRef(env, jargs);
    (*env)->DeleteLocalRef(env, runnerCls);

done:
    (*env)->DeleteLocalRef(env, runnerCls);
    complete(t->pid, result);
    (*g_jvm)->DetachCurrentThread(g_jvm);

cleanup:
    for (int i = 0; i < t->argCount; i++) free(t->args[i]);
    free(t->args);
    free(t->className);
    free(t->methodName);
    free(t);
    return NULL;
}

int java_runAsync(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    const char* method = luaL_checkstring(L, 3);
    
    // 从第4个参数起都是方法参数
    int argCount = lua_gettop(L) - 3;
    char** args = malloc(sizeof(char*) * (argCount > 0 ? argCount : 1));
    for (int i = 0; i < argCount; i++) {
        if (lua_isstring(L, 4 + i)) {
            args[i] = strdup(lua_tostring(L, 4 + i));
        } else if (lua_isinteger(L, 4 + i)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, 4 + i));
            args[i] = strdup(buf);
        } else if (lua_isnumber(L, 4 + i)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.15g", lua_tonumber(L, 4 + i));
            args[i] = strdup(buf);
        } else if (lua_isboolean(L, 4 + i)) {
            args[i] = strdup(lua_toboolean(L, 4 + i) ? "true" : "false");
        } else {
            args[i] = strdup("");
        }
    }
    if (argCount == 0) { args[0] = strdup(""); argCount = 0; }

    Task* t = malloc(sizeof(Task));
    t->pid = pid;
    t->className = strdup(cls);
    t->methodName = strdup(method);
    t->argCount = argCount;
    t->args = args;

    pthread_t tid;
    pthread_create(&tid, NULL, worker, t);
    pthread_detach(tid);
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
