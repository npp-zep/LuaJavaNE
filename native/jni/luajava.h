#ifndef LUAJAVA_H
#define LUAJAVA_H

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <jni.h>

JNIEnv* getEnv();
void throwLuaError(JNIEnv* env, lua_State* L, int errCode);

// Lua → Java 类型转换（LuaFunction 也需要用）
jobject lua_to_java_object(lua_State* L, JNIEnv* env, int idx);
void push_java_arg(lua_State* L, JNIEnv* env, jobject arg);
int new_java_object_ud(lua_State* L, jobject obj);

// ========== lua_State 引用计数 ==========
// LuaRuntime 持有 1 个引用；每次创建 LuaFunctionObj / LuaInvocationHandler（会占用注册表 ref）时再 +1。
// 引用计数归零时才真正 lua_close，避免"先关 LuaRuntime、后销毁函数对象"导致的 use-after-free。
void lua_state_add_ref(lua_State* L);    // 增加引用
int  lua_state_release_ref(lua_State* L); // 减少引用；返回 1 表示该状态应被 lua_close

#endif