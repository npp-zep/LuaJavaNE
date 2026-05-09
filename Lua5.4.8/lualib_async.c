// Lua5.4.8/lualib_async.c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;

typedef struct AsyncTask {
    lua_State* L;
    int funcRef;
    int promiseId;
} AsyncTask;

static void* run_async_thread(void* arg) {
    AsyncTask* task = (AsyncTask*)arg;
    lua_State* mainL = task->L;

    JNIEnv* env = NULL;
    if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != 0) {
        free(task);
        return NULL;
    }

    lua_settop(mainL, 0);
    lua_rawgeti(mainL, LUA_REGISTRYINDEX, task->funcRef);
    int err = lua_pcall(mainL, 0, 1, 0);

    if (err == LUA_OK) {
        char resultStr[256] = "";
        if (lua_isboolean(mainL, -1)) {
            snprintf(resultStr, sizeof(resultStr), "B:%d", lua_toboolean(mainL, -1));
        } else if (lua_isinteger(mainL, -1)) {
            snprintf(resultStr, sizeof(resultStr), "I:%lld", (long long)lua_tointeger(mainL, -1));
        } else if (lua_isnumber(mainL, -1)) {
            snprintf(resultStr, sizeof(resultStr), "N:%.15g", lua_tonumber(mainL, -1));
        } else if (lua_isstring(mainL, -1)) {
            snprintf(resultStr, sizeof(resultStr), "S:%s", lua_tostring(mainL, -1));
        } else {
            snprintf(resultStr, sizeof(resultStr), "X:");
        }
        lua_pop(mainL, 1);

        // 写回 Promise 结果（这里不应持 lua_mutex，只操作 C 链表）
        PromiseEntry* entry = promise_registry;
        while (entry) {
            if (entry->id == task->promiseId) {
                if (entry->result) free(entry->result);
                entry->result = strdup(resultStr);
                entry->done = 1;
                break;
            }
            entry = entry->next;
        }
    } else {
        // 发生错误
        const char* errMsg = lua_tostring(mainL, -1);
        PromiseEntry* entry = promise_registry;
        while (entry) {
            if (entry->id == task->promiseId) {
                if (entry->result) free(entry->result);
                char errBuf[512];
                snprintf(errBuf, sizeof(errBuf), "E:%s", errMsg ? errMsg : "unknown error");
                entry->result = strdup(errBuf);
                entry->done = 1;
                break;
            }
            entry = entry->next;
        }
        lua_pop(mainL, 1);
    }

    luaL_unref(mainL, LUA_REGISTRYINDEX, task->funcRef);
    (*g_jvm)->DetachCurrentThread(g_jvm);
    free(task);
    return NULL;
}

int java_runAsync(lua_State* L) {
    int promiseId = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

    AsyncTask* task = (AsyncTask*)malloc(sizeof(AsyncTask));
    task->L = L;
    task->funcRef = funcRef;
    task->promiseId = promiseId;

    pthread_t tid;
    pthread_create(&tid, NULL, run_async_thread, task);
    pthread_detach(tid);

    return 0;
}

int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    PromiseEntry* entry = promise_registry;
    while (entry) {
        if (entry->id == id) {
            lua_pushboolean(L, entry->done);
            if (entry->done && entry->result) {
                char* r = entry->result;
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
        entry = entry->next;
    }
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return 2;
}
