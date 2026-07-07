#include <pthread.h>
// native/lualib/lualibjava_internal.h — 内部共享声明
#ifndef LUALIBJAVA_INTERNAL_H
#define LUALIBJAVA_INTERNAL_H

#include <jni.h>
#include "lua.h"

// ========== Promise 注册表 ==========
typedef struct PromiseEntry {
    int id;
    lua_State* co;
    int done;
    char* result;
    struct PromiseEntry* next;
} PromiseEntry;

extern PromiseEntry* promise_registry;

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

// ========== lualibjava.c 导出的函数 ==========
extern int luaopen_java(lua_State* L);
extern int new_java_object_ud(lua_State* L, jobject obj);
extern jobject java_get_obj(lua_State* L, int idx);

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

#endif
