// Lua5.4.8/lualib_async.c — Agent 模式异步（主线程协程）
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;

// LuaAgent 的引用（Java 侧设置）
static jobject g_agent = NULL;
static jmethodID g_submitMethod = NULL;

// 初始化 Agent 引用
JNIEXPORT void JNICALL Java_com_luajava_LuaRuntime__1setAgent
  (JNIEnv* env, jobject obj, jlong Lptr, jobject agent) {
    if (g_agent) (*env)->DeleteGlobalRef(env, g_agent);
    g_agent = (*env)->NewGlobalRef(env, agent);
    jclass agentCls = (*env)->GetObjectClass(env, agent);
    g_submitMethod = (*env)->GetMethodID(env, agentCls, "submitTask", "(IJ)V");
    (*env)->DeleteLocalRef(env, agentCls);
}

// runAsync: 在主线程通过 Agent 执行 Lua 函数
int java_runAsync(lua_State* L) {
    int promiseId = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    
    int err = lua_pcall(L, 0, 1, 0);
    
    PromiseEntry* entry = promise_registry;
    while (entry) {
        if (entry->id == promiseId) {
            char resultStr[256] = "X:";
            if (err == LUA_OK) {
                if (lua_isboolean(L, -1)) {
                    snprintf(resultStr, sizeof(resultStr), "B:%d", lua_toboolean(L, -1));
                } else if (lua_isinteger(L, -1)) {
                    snprintf(resultStr, sizeof(resultStr), "I:%lld", (long long)lua_tointeger(L, -1));
                } else if (lua_isnumber(L, -1)) {
                    snprintf(resultStr, sizeof(resultStr), "N:%.15g", lua_tonumber(L, -1));
                } else if (lua_isstring(L, -1)) {
                    snprintf(resultStr, sizeof(resultStr), "S:%s", lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            } else {
                const char* msg = lua_tostring(L, -1);
                snprintf(resultStr, sizeof(resultStr), "E:%s", msg ? msg : "async error");
                lua_pop(L, 1);
            }
            if (entry->result) free(entry->result);
            entry->result = strdup(resultStr);
            entry->done = 1;
            break;
        }
        entry = entry->next;
    }
    return 0;
}

// checkPromise: 主线程轮询
int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    // 每次 checkPromise 时消费 Agent 队列
    if (g_agent && g_submitMethod) {
        JNIEnv* env = NULL;
        (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        if (env) {
            jclass agentCls = (*env)->GetObjectClass(env, g_agent);
            jmethodID pollMethod = (*env)->GetMethodID(env, agentCls, "poll", "(Lcom/luajava/LuaRuntime;)V");
            (*env)->DeleteLocalRef(env, agentCls);
        }
    }
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
