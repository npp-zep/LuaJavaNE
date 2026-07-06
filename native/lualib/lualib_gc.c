// native/lualib/lualib_gc.c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>

extern JavaVM* g_jvm;
extern JNIEnv* getEnv(void);

// ========== JavaUserdata 结构（与 lualibjava.c 保持一致） ==========
typedef struct {
    jobject obj;
    jclass  cls;
    int     isClass;
} JavaUserdata;

// ========== 元表名称 ==========
#define JAVAOBJECT_META "Java.Object"

// ========== 引用条目 ==========
typedef struct RefEntry {
    int id;
    jobject obj;
    int isWeak;
    struct RefEntry* next;
} RefEntry;

static RefEntry* ref_list = NULL;
static int next_ref_id = 1;

// ========== 创建 Java 对象 userdata（直接实现，不依赖外部） ==========
static int create_java_object_ud(lua_State* L, jobject obj) {
    if (!obj) {
        lua_pushnil(L);
        return 1;
    }
    
    JNIEnv* env = getEnv();
    if (!env) {
        lua_pushnil(L);
        return 1;
    }
    
    // 获取对象的类
    jclass objCls = (*env)->GetObjectClass(env, obj);
    if (!objCls) {
        lua_pushnil(L);
        return 1;
    }
    
    // 创建 userdata
    JavaUserdata* ud = (JavaUserdata*)lua_newuserdatauv(L, sizeof(JavaUserdata), 0);
    ud->obj = (*env)->NewGlobalRef(env, obj);
    ud->cls = (jclass)(*env)->NewWeakGlobalRef(env, objCls);
    ud->isClass = 0;
    
    (*env)->DeleteLocalRef(env, objCls);
    
    // 设置元表
    luaL_getmetatable(L, JAVAOBJECT_META);
    lua_setmetatable(L, -2);
    
    return 1;
}

// ========== Ref.hold ==========
static int lua_ref_hold(lua_State* L) {
    // 从 Lua 栈获取 Java 对象
    if (!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    
    // 检查是否是 Java.Object
    if (!lua_getmetatable(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    luaL_getmetatable(L, JAVAOBJECT_META);
    int isJavaObject = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    if (!isJavaObject) {
        lua_pushnil(L);
        return 1;
    }
    
    JavaUserdata* ud = (JavaUserdata*)lua_touserdata(L, 1);
    if (!ud || !ud->obj) {
        lua_pushnil(L);
        return 1;
    }
    
    JNIEnv* env = getEnv();
    if (!env) {
        lua_pushnil(L);
        return 1;
    }
    
    // 创建全局引用（强引用）
    jobject globalRef = (*env)->NewGlobalRef(env, ud->obj);
    if (!globalRef) {
        lua_pushnil(L);
        return 1;
    }
    
    RefEntry* entry = (RefEntry*)malloc(sizeof(RefEntry));
    entry->id = next_ref_id++;
    entry->obj = globalRef;
    entry->isWeak = 0;
    entry->next = ref_list;
    ref_list = entry;
    
    lua_pushinteger(L, entry->id);
    return 1;
}

// ========== Ref.holdWeak ==========
static int lua_ref_holdWeak(lua_State* L) {
    if (!lua_isuserdata(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    
    // 检查是否是 Java.Object
    if (!lua_getmetatable(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    luaL_getmetatable(L, JAVAOBJECT_META);
    int isJavaObject = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    if (!isJavaObject) {
        lua_pushnil(L);
        return 1;
    }
    
    JavaUserdata* ud = (JavaUserdata*)lua_touserdata(L, 1);
    if (!ud || !ud->obj) {
        lua_pushnil(L);
        return 1;
    }
    
    JNIEnv* env = getEnv();
    if (!env) {
        lua_pushnil(L);
        return 1;
    }
    
    jobject weakRef = (*env)->NewWeakGlobalRef(env, ud->obj);
    if (!weakRef) {
        lua_pushnil(L);
        return 1;
    }
    
    RefEntry* entry = (RefEntry*)malloc(sizeof(RefEntry));
    entry->id = next_ref_id++;
    entry->obj = weakRef;
    entry->isWeak = 1;
    entry->next = ref_list;
    ref_list = entry;
    
    lua_pushinteger(L, entry->id);
    return 1;
}

// ========== Ref.get ==========
static int lua_ref_get(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    
    RefEntry* entry = ref_list;
    while (entry) {
        if (entry->id == id) {
            JNIEnv* env = getEnv();
            if (!env) {
                lua_pushnil(L);
                return 1;
            }
            
            if (entry->isWeak) {
                jobject obj = (*env)->NewLocalRef(env, entry->obj);
                if (!obj || (*env)->IsSameObject(env, obj, NULL)) {
                    lua_pushnil(L);
                    return 1;
                }
                create_java_object_ud(L, obj);
                (*env)->DeleteLocalRef(env, obj);
            } else {
                create_java_object_ud(L, entry->obj);
            }
            return 1;
        }
        entry = entry->next;
    }
    
    lua_pushnil(L);
    return 1;
}

// ========== Ref.release ==========
static int lua_ref_release(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    
    JNIEnv* env = getEnv();
    if (!env) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    RefEntry* prev = NULL;
    RefEntry* entry = ref_list;
    while (entry) {
        if (entry->id == id) {
            if (entry->isWeak) {
                (*env)->DeleteWeakGlobalRef(env, entry->obj);
            } else {
                (*env)->DeleteGlobalRef(env, entry->obj);
            }
            
            if (prev) {
                prev->next = entry->next;
            } else {
                ref_list = entry->next;
            }
            free(entry);
            lua_pushboolean(L, 1);
            return 1;
        }
        prev = entry;
        entry = entry->next;
    }
    
    lua_pushboolean(L, 0);
    return 1;
}

// ========== Ref.exists ==========
static int lua_ref_exists(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    
    RefEntry* entry = ref_list;
    while (entry) {
        if (entry->id == id) {
            if (entry->isWeak) {
                JNIEnv* env = getEnv();
                if (!env) {
                    lua_pushboolean(L, 0);
                    return 1;
                }
                jobject obj = (*env)->NewLocalRef(env, entry->obj);
                int exists = obj && !(*env)->IsSameObject(env, obj, NULL);
                if (obj) (*env)->DeleteLocalRef(env, obj);
                lua_pushboolean(L, exists);
            } else {
                lua_pushboolean(L, 1);
            }
            return 1;
        }
        entry = entry->next;
    }
    
    lua_pushboolean(L, 0);
    return 1;
}

// ========== Ref.count ==========
static int lua_ref_count(lua_State* L) {
    int count = 0;
    RefEntry* entry = ref_list;
    while (entry) {
        count++;
        entry = entry->next;
    }
    lua_pushinteger(L, count);
    return 1;
}

// ========== Ref.clear ==========
static int lua_ref_clear(lua_State* L) {
    JNIEnv* env = getEnv();
    if (env) {
        RefEntry* entry = ref_list;
        while (entry) {
            if (entry->isWeak) {
                (*env)->DeleteWeakGlobalRef(env, entry->obj);
            } else {
                (*env)->DeleteGlobalRef(env, entry->obj);
            }
            RefEntry* next = entry->next;
            free(entry);
            entry = next;
        }
        ref_list = NULL;
        next_ref_id = 1;
    }
    return 0;
}

// ========== Ref.list ==========
static int lua_ref_list(lua_State* L) {
    lua_newtable(L);
    int idx = 1;
    RefEntry* entry = ref_list;
    while (entry) {
        lua_pushinteger(L, entry->id);
        lua_rawseti(L, -2, idx++);
        entry = entry->next;
    }
    return 1;
}

// ========== 库注册 ==========
static const luaL_Reg reflib[] = {
    {"hold",      lua_ref_hold},
    {"holdWeak",  lua_ref_holdWeak},
    {"get",       lua_ref_get},
    {"release",   lua_ref_release},
    {"exists",    lua_ref_exists},
    {"count",     lua_ref_count},
    {"clear",     lua_ref_clear},
    {"list",      lua_ref_list},
    {NULL, NULL}
};

int luaopen_gc(lua_State* L) {
    // 确保 Java.Object 元表存在（由 lualibjava.c 创建）
    // 但为了安全，我们检查一下
    luaL_getmetatable(L, JAVAOBJECT_META);
    if (lua_isnil(L, -1)) {
        // 如果元表不存在，创建一个简单的
        lua_pop(L, 1);
        luaL_newmetatable(L, JAVAOBJECT_META);
        lua_pushstring(L, "__index");
        lua_pushvalue(L, -2);
        lua_settable(L, -3);
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    
    luaL_newlib(L, reflib);
    return 1;
}