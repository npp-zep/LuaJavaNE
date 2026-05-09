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
    char* arg;
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

static void* worker(void* arg) {
    Task* t = (Task*)arg;
    JNIEnv* env = NULL;
    if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != 0) {
        complete(t->pid, "E:attach failed");
        free(t->className); free(t->methodName); free(t);
    free(t->arg);
        return NULL;
    }

    char result[512] = "X:";
    // 替换 . 为 /
    char* desc = strdup(t->className);
    for (char* p = desc; *p; p++) if (*p == '.') *p = '/';

    jclass runnerCls = (*env)->FindClass(env, "com/luajava/AsyncRunner");
    if (runnerCls) {
        jmethodID runMid = (*env)->GetStaticMethodID(env, runnerCls, "runStatic",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (runMid) {
            jstring jcls = (*env)->NewStringUTF(env, t->className);
            jstring jmethod = (*env)->NewStringUTF(env, t->methodName);
            jstring jarg = (*env)->NewStringUTF(env, t->arg ? t->arg : "");
            jstring jres = (jstring)(*env)->CallStaticObjectMethod(env, runnerCls, runMid, jcls, jmethod, jarg);
            if (jres) {
                const char* s = (*env)->GetStringUTFChars(env, jres, NULL);
                snprintf(result, sizeof(result), "S:%s", s);
                (*env)->ReleaseStringUTFChars(env, jres, s);
                (*env)->DeleteLocalRef(env, jres);
            }
            (*env)->DeleteLocalRef(env, jcls);
            (*env)->DeleteLocalRef(env, jmethod);
            (*env)->DeleteLocalRef(env, jarg);
        }
        (*env)->DeleteLocalRef(env, runnerCls);
    }

    complete(t->pid, result);
    free(t->className); free(t->methodName); free(t);
    free(t->arg);
    (*g_jvm)->DetachCurrentThread(g_jvm);
    return NULL;
}

int java_runAsync(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    const char* method = luaL_checkstring(L, 3);
    const char* arg = luaL_optstring(L, 4, "");

    Task* t = malloc(sizeof(Task));
    t->pid = pid;
    t->className = strdup(cls);
    t->methodName = strdup(method);
    t->arg = strdup(arg);

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
