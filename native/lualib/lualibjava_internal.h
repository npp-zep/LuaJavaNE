#include <pthread.h>
// native/lualib/lualibjava_internal.h — 内部共享声明
#ifndef LUALIBJAVA_INTERNAL_H
#define LUALIBJAVA_INTERNAL_H

#include <jni.h>
#include "lua.h"

// ========== Promise 注册表 ==========
typedef struct PromiseEntry {
    int id;
    lua_State* co;          // 协程 await 用（可选保留）
    lua_State* owner;       // 创建/注册回调所属的 Lua 状态
    int callbackRef;        // 回调闭包在 owner 注册表中的引用，LUA_NOREF 表示无
    int done;
    char* result;
    struct PromiseEntry* next;
} PromiseEntry;

extern PromiseEntry* promise_registry;
extern pthread_mutex_t promise_mutex;   // lualib_async.c 定义，保护 Promise 注册表

typedef struct {
    jobject obj;
    jclass cls;
    int isClass;
} JavaUserdata;

// ========== lualib_async.c 导出的函数 ==========
extern int java_runAsync(lua_State* L);
extern int java_runAsyncObj(lua_State* L);
extern int java_checkPromise(lua_State* L);
extern int java_getObject(lua_State* L);
extern void java_promise_cleanup_state(lua_State* L);   // 状态关闭时清理其回调引用
extern void dispatch_callback(lua_State* owner, int cbRef, const char* result); // 派发回调闭包

// ========== lualibjava.c 导出的函数 ==========
extern int luaopen_java(lua_State* L);
extern int new_java_object_ud(lua_State* L, jobject obj);
extern jobject java_get_obj(lua_State* L, int idx);
extern PromiseEntry* promise_find(int id);   // 调用方须持有 promise_mutex
extern void promise_remove(PromiseEntry* e); // 调用方须持有 promise_mutex

// ========== lualib_clac.c 导出的函数 ==========
extern int luaopen_clac(lua_State* L);

// ========== lua_custom_init.c 导出的函数 ==========
extern void lua_open_custom_libs(lua_State* L);

// ========== luajava.c (JNI) 导出的函数/变量 ==========
extern JavaVM* g_jvm;
extern pthread_mutex_t lua_mutex;
extern JNIEnv* getEnv(void);
extern void throwLuaError(JNIEnv* env, lua_State* L, int errCode);
extern void push_java_arg(lua_State* L, JNIEnv* env, jobject arg);
extern jobject lua_to_java_object(lua_State* L, JNIEnv* env, int idx);
extern void lua_state_add_ref(lua_State* L);    // lua_State 引用计数 +1
extern int  lua_state_release_ref(lua_State* L); // lua_State 引用计数 -1；返回 1 表示应 lua_close

#endif
