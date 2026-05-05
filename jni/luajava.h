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
extern pthread_mutex_t lua_mutex;

#endif