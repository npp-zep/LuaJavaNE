#include <jni.h>
// Lua5.4.8/lualibjava_internal.h — 内部共享声明
#ifndef LUALIBJAVA_INTERNAL_H
#define LUALIBJAVA_INTERNAL_H

#include "lua.h"

// Promise 注册表（lualibjava.c 定义，lualib_async.c 引用）
typedef struct PromiseEntry {
    int id;
    lua_State* co;
    int done;
    char* result;
    struct PromiseEntry* next;
} PromiseEntry;

extern PromiseEntry* promise_registry;

// java_runAsync 和 java_checkPromise 供 lualibjava.c 的 javalib 注册
extern int java_runAsync(lua_State* L);
extern int java_checkPromise(lua_State* L);
extern jobject java_get_obj(lua_State* L, int idx);

#endif

