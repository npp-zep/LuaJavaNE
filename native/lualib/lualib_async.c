#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lualibjava_internal.h"
#include "utf8_utils.h"
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../jni_compat.h"

extern JavaVM* g_jvm;
extern int new_java_object_ud(lua_State* L, jobject obj);

pthread_mutex_t promise_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========== 共享解析器：把序列化结果串按前缀压入栈 ==========
// 返回压入栈的值的个数；供 checkPromise 与回调派发共用，保证结果语义一致
int java_push_parsed_result(lua_State* L, const char* r) {
    int pushed = 0;
    if (!r || !r[0]) return pushed;
    switch (r[0]) {
        case 'M': {
            // 格式：M:<count>|<type>:<len>:<payload>...
            // 每个元素带长度前缀，字符串中可安全包含 ':' 或 '|'。
            int count = atoi(r + 2);   // 跳过 "M:"
            const char* p = strchr(r, '|');
            if (!p) { lua_pushnil(L); pushed = 1; break; }
            p++;
            for (int i = 0; i < count && p && *p; i++) {
                char type = *p;
                p += 2;                 // 跳过 "<type>:"
                int len = 0;
                while (*p >= '0' && *p <= '9') { len = len * 10 + (*p - '0'); p++; }
                if (*p == ':') p++;
                switch (type) {
                    case 'S': lua_pushlstring(L, p, (size_t)len); break;
                    case 'I': lua_pushinteger(L, (lua_Integer)strtoll(p, NULL, 10)); break;
                    case 'N': lua_pushnumber(L, (lua_Number)strtod(p, NULL)); break;
                    case 'B': lua_pushboolean(L, len > 0 && (*p == 't' || *p == '1')); break;
                    case 'O': lua_pushinteger(L, (lua_Integer)atoi(p)); break;
                    case 'X': lua_pushnil(L); break;
                    default: lua_pushnil(L);
                }
                pushed++;
                p += len;
            }
            break;
        }
        case 'S': lua_pushstring(L, r + 2); pushed = 1; break;
        case 'I': lua_pushinteger(L, atoll(r + 2)); pushed = 1; break;
        case 'N': lua_pushnumber(L, atof(r + 2)); pushed = 1; break;
        case 'B': lua_pushboolean(L, (r[2] == 't' || r[2] == '1')); pushed = 1; break;
        case 'O': lua_pushinteger(L, atoi(r + 2)); pushed = 1; break;
        case 'E': lua_pushstring(L, r + 2); pushed = 1; break;
        default:  lua_pushnil(L); pushed = 1; break;
    }
    return pushed;
}

// ========== 回调派发（在工作线程上执行） ==========
// 锁序：只持有 lua_mutex（递归锁），不再持有 promise_mutex，避免与主线程死锁。
// 调用方必须先拷贝出 cbRef/owner/result 并释放 promise_mutex。
void dispatch_callback(lua_State* owner, int cbRef, const char* result) {
    if (cbRef == LUA_NOREF || !owner) return;
    pthread_mutex_lock(&lua_mutex);
    int base = lua_gettop(owner);   // 记录进入时的栈底，防止把调用方遗留的栈值误当参数
    lua_rawgeti(owner, LUA_REGISTRYINDEX, cbRef);
    if (lua_isfunction(owner, -1)) {
        // 参数约定：callback(err, result...)；err 为 E: 错误串，成功时为 nil
        if (result && result[0] == 'E') {
            lua_pushstring(owner, result + 2);
        } else {
            lua_pushnil(owner);
            if (result && result[0]) java_push_parsed_result(owner, result);
            else lua_pushnil(owner);
        }
        int nargs = lua_gettop(owner) - (base + 1);   // 函数之上的参数个数
        if (lua_pcall(owner, nargs, 0, 0) != LUA_OK) {
            const char* msg = lua_tostring(owner, -1);
            fprintf(stderr, "java.onComplete callback error: %s\n", msg ? msg : "unknown");
            lua_pop(owner, 1);
        }
        luaL_unref(owner, LUA_REGISTRYINDEX, cbRef);
    } else {
        lua_pop(owner, 1);
    }
    pthread_mutex_unlock(&lua_mutex);
}

JNIEXPORT void JNICALL Java_com_luajava_LuaAgent_complete
  (JNIEnv* env, jclass cls, jint pid, jstring result) {
    char* s = java_string_to_utf8(env, result, NULL);
    if (!s) return;

    lua_State* owner = NULL;
    lua_State* co = NULL;
    int cbRef = LUA_NOREF;
    char* copy = NULL;

    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_find(pid);
    if (e) {
        if (e->result) free(e->result);
        e->result = strdup(s);
        e->done = 1;
        co = e->co;                 // 等待中的协程，完成后由本线程恢复
        e->co = NULL;
        if (e->callbackRef != LUA_NOREF) {
            owner = e->owner;
            cbRef = e->callbackRef;
            copy = strdup(s);
            e->callbackRef = LUA_NOREF;
            if (!co) promise_remove(e);
        }
    }
    pthread_mutex_unlock(&promise_mutex);

    // 先释放 promise_mutex 再进 Lua，锁序：promise_mutex → 释放 → lua_mutex
    if (co) {
        pthread_mutex_lock(&lua_mutex);
        // 若状态已被 close/cleanup 移除，则协程指针可能已失效，放弃恢复
        pthread_mutex_lock(&promise_mutex);
        int alive = (promise_find(pid) != NULL);
        pthread_mutex_unlock(&promise_mutex);
        if (alive) {
            int n = java_push_parsed_result(co, s);
            int nres = 0;
            int rc = lua_resume(co, owner, n, &nres);
            if (rc != LUA_OK && rc != LUA_YIELD) {
                const char* msg = lua_tostring(co, -1);
                fprintf(stderr, "java.await resume error: %s\n", msg ? msg : "unknown");
                lua_pop(co, 1);
            }
        }
        pthread_mutex_unlock(&lua_mutex);
    }
    if (copy) {
        dispatch_callback(owner, cbRef, copy);
        free(copy);
    }
    free(s);
}

// 状态关闭时清理该状态注册的回调引用，避免悬空
void java_promise_cleanup_state(lua_State* L) {
    if (!L) return;
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry** pp = &promise_registry;
    while (*pp) {
        PromiseEntry* e = *pp;
        if (e->owner == L) {
            if (e->callbackRef != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, e->callbackRef);
                e->callbackRef = LUA_NOREF;
            }
            *pp = e->next;
            if (e->result) free(e->result);
            free(e);
        } else {
            pp = &e->next;
        }
    }
    pthread_mutex_unlock(&promise_mutex);
}

static char get_type_hint(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER: return lua_isinteger(L, idx) ? 'I' : 'D';
        case LUA_TSTRING: return 'S';
        case LUA_TBOOLEAN: return 'Z';
        default: return 'S';
    }
}

// 把 Lua 参数转成字符串传给 Java，配合 get_type_hint 的类型提示。
// 数值使用足够精度（整数 %lld、浮点 %.17g），避免 lua_tostring 的精度损失；
// 布尔转为 "true"/"false"；其他类型按 lua_tostring 处理（表/函数为 ""）。
// 仅真正的 Lua number 才按数值渲染：lua_isnumber 会把可转数字的字符串也当成数字，
// 导致 "123..." 这样的字符串按 %.17g 丢精度（与 'S' 类型提示不一致）。
static const char* arg_to_string(lua_State* L, int idx, char* buf, size_t bufsz) {
    if (lua_type(L, idx) != LUA_TNUMBER) {
        if (lua_type(L, idx) == LUA_TBOOLEAN) {
            return lua_toboolean(L, idx) ? "true" : "false";
        }
        const char* s = lua_tostring(L, idx);
        return s ? s : "";
    }
    if (lua_isinteger(L, idx)) {
        snprintf(buf, bufsz, "%lld", (long long)lua_tointeger(L, idx));
        return buf;
    }
    snprintf(buf, bufsz, "%.17g", (double)lua_tonumber(L, idx));
    return buf;
}

// 构造 AgentTask 的 [value,hint] 参数对：Java 对象经对象池传 id（hint='O'），
// 避免把 userdata 字符串化成空串。返回值类型由 AsyncRunner 从池中取回。
static void fill_task_args(lua_State* L, JNIEnv* env, int firstIdx, int argCount,
                           jobjectArray jargs) {
    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID regMid = (*env)->GetStaticMethodID(env, agentCls, "registerArgumentObject", "(Ljava/lang/Object;)I");
    for (int i = 0; i < argCount; i++) {
        int idx = firstIdx + i;
        char buf[64];
        const char* val;
        char hint;
        jobject obj = (lua_type(L, idx) == LUA_TUSERDATA) ? java_ud_to_object(L, idx) : NULL;
        if (obj && regMid) {
            jint oid = (*env)->CallStaticIntMethod(env, agentCls, regMid, obj);
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                val = arg_to_string(L, idx, buf, sizeof(buf));
                hint = get_type_hint(L, idx);
            } else {
                snprintf(buf, sizeof(buf), "%d", (int)oid);
                val = buf;
                hint = 'O';
            }
        } else {
            val = arg_to_string(L, idx, buf, sizeof(buf));
            hint = get_type_hint(L, idx);
        }
        char hbuf[2] = { hint, '\0' };
        jstring jval = utf8_to_jstring(env, val, strlen(val));
        jstring jhint = (*env)->NewStringUTF(env, hbuf);
        (*env)->SetObjectArrayElement(env, jargs, i * 2, jval);
        (*env)->SetObjectArrayElement(env, jargs, i * 2 + 1, jhint);
        (*env)->DeleteLocalRef(env, jval);
        (*env)->DeleteLocalRef(env, jhint);
    }
    (*env)->DeleteLocalRef(env, agentCls);
}

int java_runAsync(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    const char* mtd = luaL_checkstring(L, 3);

    int argCount = lua_gettop(L) - 3;
    int pairCount = argCount * 2;

    int expand = 0;
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* pe = promise_find(pid);
    if (pe) expand = pe->expandArrays;
    pthread_mutex_unlock(&promise_mutex);

    JNIEnv* env = NULL;
    JNI_ATTACH(g_jvm, env);
    if (!env) return 0;

    jclass taskCls = (*env)->FindClass(env, "com/luajava/AgentTask");
    jmethodID ctor = (*env)->GetMethodID(env, taskCls, "<init>", "(ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;Z)V");
    jstring jcls = (*env)->NewStringUTF(env, cls);
    jstring jmtd = (*env)->NewStringUTF(env, mtd);

    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray jargs = (*env)->NewObjectArray(env, pairCount > 0 ? pairCount : 0, strCls, NULL);
    fill_task_args(L, env, 4, argCount, jargs);

    jobject task = (*env)->NewObject(env, taskCls, ctor, (jint)pid, jcls, jmtd, jargs, (jboolean)expand);
    (*env)->DeleteLocalRef(env, jcls);
    (*env)->DeleteLocalRef(env, jmtd);
    (*env)->DeleteLocalRef(env, jargs);
    (*env)->DeleteLocalRef(env, taskCls);

    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID submitMid = (*env)->GetStaticMethodID(env, agentCls, "submitTask", "(Lcom/luajava/AgentTask;)V");
    if (submitMid) {
        (*env)->CallStaticVoidMethod(env, agentCls, submitMid, task);
    }
    (*env)->DeleteLocalRef(env, task);
    (*env)->DeleteLocalRef(env, agentCls);

    return 0;
}

int java_runAsyncObj(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TUSERDATA);
    const char* method = luaL_checkstring(L, 3);
    int argCount = lua_gettop(L) - 3;
    int pairCount = argCount * 2;

    int expand = 0;
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* pe = promise_find(pid);
    if (pe) expand = pe->expandArrays;
    pthread_mutex_unlock(&promise_mutex);

    JNIEnv* env = NULL;
    JNI_ATTACH(g_jvm, env);
    if (!env) return 0;

    jclass taskCls = (*env)->FindClass(env, "com/luajava/AgentTask");
    jmethodID ctor = (*env)->GetMethodID(env, taskCls, "<init>", "(ILjava/lang/Object;Ljava/lang/String;[Ljava/lang/String;Z)V");
    jstring jmtd = (*env)->NewStringUTF(env, method);

    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray jargs = (*env)->NewObjectArray(env, pairCount > 0 ? pairCount : 0, strCls, NULL);
    fill_task_args(L, env, 4, argCount, jargs);

    jobject jobj = java_get_obj(L, 2);
    jobject task = (*env)->NewObject(env, taskCls, ctor, (jint)pid, jobj, jmtd, jargs, (jboolean)expand);
    (*env)->DeleteLocalRef(env, jmtd);
    (*env)->DeleteLocalRef(env, jargs);
    (*env)->DeleteLocalRef(env, taskCls);

    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID submitMid = (*env)->GetStaticMethodID(env, agentCls, "submitTask", "(Lcom/luajava/AgentTask;)V");
    if (submitMid) (*env)->CallStaticVoidMethod(env, agentCls, submitMid, task);
    (*env)->DeleteLocalRef(env, task);
    (*env)->DeleteLocalRef(env, agentCls);
    return 0;
}

int java_checkPromise(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_find(id);
    if (!e) {
        pthread_mutex_unlock(&promise_mutex);
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        return 2;
    }
    lua_pushboolean(L, e->done);
    int n = 1;
    if (e->done && e->result) {
        int pushed = java_push_parsed_result(L, e->result);
        n += pushed;
        // 已消费 + 无回调 → 清理条目（修复泄漏）
        if (e->callbackRef == LUA_NOREF) {
            promise_remove(e);
        }
    } else {
        lua_pushnil(L);
        n++;
    }
    pthread_mutex_unlock(&promise_mutex);
    return n;
}

int java_getObject(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    JNIEnv* env = NULL;
    JNI_ATTACH(g_jvm, env);
    if (!env) { lua_pushnil(L); return 1; }

    jclass agentCls = (*env)->FindClass(env, "com/luajava/LuaAgent");
    jmethodID getObjMid = (*env)->GetStaticMethodID(env, agentCls, "getObject", "(I)Ljava/lang/Object;");
    if (!getObjMid) { (*env)->ExceptionClear(env); lua_pushnil(L); (*env)->DeleteLocalRef(env, agentCls); return 1; }

    jobject obj = (*env)->CallStaticObjectMethod(env, agentCls, getObjMid, (jint)id);
    if (!obj) {
        // 区分“不存在/已被取走”与“对象池溢出被淘汰”：后者是数据丢失，明确报错而不是静默 nil
        jmethodID evictedMid = (*env)->GetStaticMethodID(env, agentCls, "wasEvicted", "(I)Z");
        jboolean evicted = JNI_FALSE;
        if (evictedMid) {
            evicted = (*env)->CallStaticBooleanMethod(env, agentCls, evictedMid, (jint)id);
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                evicted = JNI_FALSE;
            }
        } else {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, agentCls);
        if (evicted) {
            return luaL_error(L, "async result %d was discarded: async result pool exceeded its capacity (too many unconsumed results)", id);
        }
        lua_pushnil(L);
        return 1;
    }
    (*env)->DeleteLocalRef(env, agentCls);

    new_java_object_ud(L, obj);  // 使用 lualibjava.c 中的函数包装
    (*env)->DeleteLocalRef(env, obj);
    return 1;
}
