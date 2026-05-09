// Lua5.4.8/lualib_async.c — 多线程异步（独立 lua_State 执行 Lua 函数，Promise 回传）
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;
extern int luaopen_java(lua_State* L);

// dump 缓冲区
typedef struct { char* buf; size_t len; size_t cap; } ByteBuf;
static int buf_writer(lua_State* L, const void* p, size_t sz, void* ud) {
    ByteBuf* bb = (ByteBuf*)ud;
    if (bb->len + sz > bb->cap) { bb->cap = (bb->cap + sz)*2; bb->buf = realloc(bb->buf, bb->cap); }
    memcpy(bb->buf + bb->len, p, sz); bb->len += sz;
    return 0;
}

// 异步任务
typedef struct { char* bc; size_t len; int pid; } Task;

// 写 Promise 结果
static void complete(int pid, const char* s) {
    PromiseEntry* e = promise_registry;
    while (e) { if (e->id == pid) { if (e->result) free(e->result); e->result = strdup(s); e->done = 1; return; } e = e->next; }
}

static void* worker(void* arg) {
    Task t = *(Task*)arg; free(arg);
    JNIEnv* env = NULL;
    if ((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != 0) { free(t.bc); return NULL; }

    lua_State* L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);
        luaL_requiref(L, "java", luaopen_java, 1); lua_pop(L, 1);
        lua_pushstring(L, "luajava_stateptr"); lua_pushinteger(L, (lua_Integer)(uintptr_t)L); lua_settable(L, LUA_REGISTRYINDEX);

        int err = luaL_loadbuffer(L, t.bc, t.len, "async");
        if (err == LUA_OK) { err = lua_pcall(L, 0, 1, 0); }
        char r[256] = "X:";
        if (err == LUA_OK) {
            if (lua_isboolean(L,-1)) snprintf(r,sizeof(r),"B:%d",lua_toboolean(L,-1));
            else if (lua_isinteger(L,-1)) snprintf(r,sizeof(r),"I:%lld",(long long)lua_tointeger(L,-1));
            else if (lua_isnumber(L,-1)) snprintf(r,sizeof(r),"N:%.15g",lua_tonumber(L,-1));
            else if (lua_isstring(L,-1)) snprintf(r,sizeof(r),"S:%s",lua_tostring(L,-1));
        } else { const char* m = lua_tostring(L,-1); snprintf(r,sizeof(r),"E:%s",m?m:"error"); }
        lua_close(L);
        complete(t.pid, r);
    }
    free(t.bc);
    (*g_jvm)->DetachCurrentThread(g_jvm);
    return NULL;
}

int java_runAsync(lua_State* L) {
    int pid = (int)luaL_checkinteger(L,1); luaL_checktype(L,2,LUA_TFUNCTION);
    ByteBuf bb = {NULL,0,0}; lua_pushvalue(L,2); lua_dump(L, buf_writer, &bb, 0); lua_pop(L,1);
    Task* t = malloc(sizeof(Task)); t->bc = bb.buf; t->len = bb.len; t->pid = pid;
    pthread_t tid; pthread_create(&tid, NULL, worker, t); pthread_detach(tid);
    return 0;
}

int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L,1);
    PromiseEntry* e = promise_registry;
    while (e) { if (e->id == id) { lua_pushboolean(L,e->done); if (e->done && e->result) { char* r=e->result;
        switch(r[0]) { case 'S':lua_pushstring(L,r+2);break; case 'I':lua_pushinteger(L,atoll(r+2));break;
        case 'N':lua_pushnumber(L,atof(r+2));break; case 'B':lua_pushboolean(L,r[2]=='1');break;
        case 'E':lua_pushstring(L,r+2);lua_error(L);break; default:lua_pushnil(L); } }
        else lua_pushnil(L); return 2; } e = e->next; }
    lua_pushboolean(L,0); lua_pushnil(L); return 2;
}
