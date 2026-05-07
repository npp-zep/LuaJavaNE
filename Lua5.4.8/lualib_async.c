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

    // 等待主线程释放锁（yield 后 unlock）
    pthread_mutex_lock(&lua_mutex);

    lua_rawgeti(mainL, LUA_REGISTRYINDEX, task->funcRef);
    int err = lua_pcall(mainL, 0, 1, 0);

    if (err == LUA_OK) {
        PromiseEntry* entry = promise_registry;
        while (entry) {
            if (entry->id == task->promiseId) break;
            entry = entry->next;
        }

        if (entry && entry->co) {
            int nres;
            // 从 mainL 栈顶取函数返回值传给协程
            lua_resume(entry->co, mainL, 1, &nres);
        }
        if (entry) entry->done = 1;
        lua_pop(mainL, 1);
    }

    luaL_unref(mainL, LUA_REGISTRYINDEX, task->funcRef);
    pthread_mutex_unlock(&lua_mutex);
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
