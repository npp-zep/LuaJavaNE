// Lua5.4.8/lualib_async.c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;
extern pthread_mutex_t lua_mutex;

typedef struct PromiseEntry {
    int id;
    lua_State* co;
    int done;
    char* result;
    struct PromiseEntry* next;
} PromiseEntry;

extern PromiseEntry* promise_registry;

typedef struct AsyncTask {
    lua_State* L;
    int funcRef;
    int promiseId;
} AsyncTask;

static void* run_async_thread(void* arg) {
    AsyncTask* task = (AsyncTask*)arg;
    lua_State* mainL = task->L;

    JNIEnv* env = NULL;
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

    lua_settop(mainL, 0);
    lua_rawgeti(mainL, LUA_REGISTRYINDEX, task->funcRef);
    int err = lua_pcall(mainL, 0, 1, 0);

    if (err == LUA_OK) {
        const char* val = lua_tostring(mainL, -1);
        lua_pop(mainL, 1);

        PromiseEntry* entry = promise_registry;
        while (entry) { if (entry->id == task->promiseId) break; entry = entry->next; }
        if (entry) {
            if (entry->result) free(entry->result);
            entry->result = val ? strdup(val) : strdup("");
            entry->done = 1;
        }
    }

    luaL_unref(mainL, LUA_REGISTRYINDEX, task->funcRef);
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
            if (entry->done && entry->result) lua_pushstring(L, entry->result);
            else lua_pushnil(L);
            return 2;
        }
        entry = entry->next;
    }
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return 2;
}
