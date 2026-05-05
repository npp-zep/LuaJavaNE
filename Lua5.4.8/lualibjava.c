// Lua5.4.8/lualibjava.c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern JNIEnv* getEnv();

typedef struct {
    jobject obj;
    jclass  cls;
    int     isClass;
} JavaUserdata;

typedef struct {
    jobject   obj;
    char*     methodName;
    int       isStatic;
} MethodLookup;

typedef struct {
    jobject arrayObj;
    jclass  elementClass;
    char    elementType;
    int     length;
} JavaArray;

typedef struct {
    lua_State* L;
    int        tableRef;
} ProxyHandler;

#define JAVACLASS_META    "Java.Class"
#define JAVAOBJECT_META   "Java.Object"
#define METHODLOOKUP_META "Java.MethodLookup"
#define JAVAARRAY_META    "Java.Array"

// ========== 前向声明 ==========
static int method_lookup_call(lua_State* L);
static int method_lookup_gc(lua_State* L);
//static int java_object_tostring(lua_State* L);
static int java_object_index(lua_State* L);
static int java_object_newindex(lua_State* L);
static int java_object_gc(lua_State* L);
static int java_class_call(lua_State* L);
static int java_class_index(lua_State* L);
static int java_class_newindex(lua_State* L);
static int java_class_tostring(lua_State* L);

// ========== 获取类名 ==========
static char* get_class_name_from_classobj(JNIEnv* env, jclass cls) {
    jclass classClass = (*env)->GetObjectClass(env, cls);
    if (!classClass || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return strdup("unknown");
    }
    jmethodID getName = (*env)->GetMethodID(env, classClass, "getName", "()Ljava/lang/String;");
    if (!getName || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, classClass);
        return strdup("unknown");
    }
    jstring name = (jstring)(*env)->CallObjectMethod(env, cls, getName);
    if (!name || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, classClass);
        return strdup("unknown");
    }
    const char* cname = (*env)->GetStringUTFChars(env, name, NULL);
    char* result = strdup(cname ? cname : "unknown");
    (*env)->ReleaseStringUTFChars(env, name, cname);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, classClass);
    return result;
}

static jstring lua_check_jstring(JNIEnv* env, lua_State* L, int idx) {
    size_t len;
    const char* str = luaL_checklstring(L, idx, &len);
    return (*env)->NewStringUTF(env, str);
}

static char get_java_type_char(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER: return lua_isinteger(L, idx) ? 'I' : 'D';
        case LUA_TSTRING: return 'S';
        case LUA_TBOOLEAN: return 'Z';
        case LUA_TUSERDATA: {
            if (lua_getmetatable(L, idx)) {
                luaL_getmetatable(L, JAVAARRAY_META);
                int isArray = lua_rawequal(L, -1, -2);
                lua_pop(L, 2);
                if (isArray) return 'A';
            }
            return 'O';
        }
        default: return 'O';
    }
}

// ========== 创建 userdata ==========
static int new_java_class_ud(lua_State* L, jclass cls) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)lua_newuserdatauv(L, sizeof(JavaUserdata), 0);
    ud->obj = (*env)->NewGlobalRef(env, cls);
    ud->cls = (jclass)(*env)->NewWeakGlobalRef(env, cls);
    ud->isClass = 1;
    luaL_getmetatable(L, JAVACLASS_META);
    lua_setmetatable(L, -2);
    return 1;
}

int new_java_object_ud(lua_State* L, jobject obj) {
    if (!obj) { lua_pushnil(L); return 1; }
    JNIEnv* env = getEnv();

    jclass objCls = (*env)->GetObjectClass(env, obj);

    // 检查是否是数组
    jclass classClass = (*env)->GetObjectClass(env, objCls);
    jmethodID getName = (*env)->GetMethodID(env, classClass, "getName", "()Ljava/lang/String;");
    jstring className = (jstring)(*env)->CallObjectMethod(env, objCls, getName);
    const char* cname = (*env)->GetStringUTFChars(env, className, NULL);
    int isArray = (cname[0] == '[');
    (*env)->ReleaseStringUTFChars(env, className, cname);
    (*env)->DeleteLocalRef(env, className);
    (*env)->DeleteLocalRef(env, classClass);

    if (isArray) {
        // ... 数组分支 ...
    }

    (*env)->DeleteLocalRef(env, objCls);

    JavaUserdata* ud = (JavaUserdata*)lua_newuserdatauv(L, sizeof(JavaUserdata), 0);

    ud->obj = (*env)->NewGlobalRef(env, obj);

    jclass objCls2 = (*env)->GetObjectClass(env, obj);
    ud->cls = (jclass)(*env)->NewWeakGlobalRef(env, objCls2);
    (*env)->DeleteLocalRef(env, objCls2);

    ud->isClass = 0;

    luaL_getmetatable(L, JAVAOBJECT_META);
    lua_setmetatable(L, -2);
    return 1;
}

static int new_method_lookup(lua_State* L, jobject obj, const char* name, int isStatic) {
    JNIEnv* env = getEnv();
    MethodLookup* ml = (MethodLookup*)lua_newuserdatauv(L, sizeof(MethodLookup), 0);
    ml->obj        = (*env)->NewGlobalRef(env, obj);
    ml->methodName = strdup(name);
    ml->isStatic   = isStatic;
    luaL_getmetatable(L, METHODLOOKUP_META);
    lua_setmetatable(L, -2);
    return 1;
}

// ========== 类型推导辅助 ==========
static void get_possible_arg_types(char c, char** options, int* count) {
    static char intOptions[4][32]    = {"I","D","J","Ljava/lang/Object;"};
    static char doubleOptions[2][32] = {"D","Ljava/lang/Object;"};
    static char stringOptions[2][32] = {"Ljava/lang/String;","Ljava/lang/Object;"};
    static char boolOptions[2][32]   = {"Z","Ljava/lang/Object;"};
    static char objOptions[1][32]    = {"Ljava/lang/Object;"};
    static char* optPtrs[3];
    switch (c) {
        case 'I': optPtrs[0]=intOptions[0];optPtrs[1]=intOptions[1];optPtrs[2]=intOptions[2];optPtrs[3]=intOptions[3];*options=(char*)optPtrs;*count=4;break;
        case 'D': optPtrs[0]=doubleOptions[0];optPtrs[1]=doubleOptions[1];*options=(char*)optPtrs;*count=2;break;
        case 'S': optPtrs[0]=stringOptions[0];optPtrs[1]=stringOptions[1];*options=(char*)optPtrs;*count=2;break;
        case 'Z': optPtrs[0]=boolOptions[0];optPtrs[1]=boolOptions[1];*options=(char*)optPtrs;*count=2;break;
        case 'A': optPtrs[0]=objOptions[0];*options=(char*)optPtrs;*count=1;break;
        default:  optPtrs[0]=objOptions[0];*options=(char*)optPtrs;*count=1;break;
    }
}

static const char* lua_type_to_jni_sig(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER: return lua_isinteger(L,idx)?"I":"D";
        case LUA_TSTRING: return "Ljava/lang/String;";
        case LUA_TBOOLEAN: return "Z";
        default: return "Ljava/lang/Object;";
    }
}

static jmethodID try_method_combinations(JNIEnv* env, jclass cls, const char* name,
                                          lua_State* L, int startIdx, int nargs,
                                          char returnType, int argIdx,
                                          char* sigBuf, char* outReturnType, int isStatic) {
    if (argIdx == nargs) {
        strcat(sigBuf, ")");
        switch (returnType) {
            case 'S': strcat(sigBuf,"Ljava/lang/String;");break;
            case 'I': strcat(sigBuf,"I");break;
            case 'D': strcat(sigBuf,"D");break;
            case 'Z': strcat(sigBuf,"Z");break;
            case 'V': strcat(sigBuf,"V");break;
            case 'J': strcat(sigBuf,"J");break;
            default:  strcat(sigBuf,"Ljava/lang/Object;");break;
        }
        jmethodID method = isStatic ?
            (*env)->GetStaticMethodID(env, cls, name, sigBuf) :
            (*env)->GetMethodID(env, cls, name, sigBuf);
        if (method && !(*env)->ExceptionCheck(env)) {
            if (outReturnType) *outReturnType = returnType;
            return method;
        }
        (*env)->ExceptionClear(env);
        return NULL;
    }
    char c = get_java_type_char(L, startIdx + argIdx);
    char* options; int count;
    get_possible_arg_types(c, &options, &count);
    for (int i = 0; i < count; i++) {
        char saved[128]; strcpy(saved, sigBuf);
        strcat(sigBuf, ((char**)options)[i]);
        jmethodID m = try_method_combinations(env, cls, name, L, startIdx, nargs, returnType, argIdx+1, sigBuf, outReturnType, isStatic);
        if (m) return m;
        strcpy(sigBuf, saved);
    }
    return NULL;
}

static jmethodID try_find_method(JNIEnv* env, jclass cls, const char* name,
                                  lua_State* L, int startIdx, int nargs,
                                  char* outReturnType, int isStatic) {
    char returnTypes[] = {'S','I','Z','D','J','V','O',0};
    for (char* rt=returnTypes; *rt; rt++) {
        char sig[128] = "(";
        jmethodID m = try_method_combinations(env, cls, name, L, startIdx, nargs, *rt, 0, sig, outReturnType, isStatic);
        if (m) return m;
    }
    return NULL;
}

static void push_jni_args(lua_State* L, JNIEnv* env, int startIdx, int nargs, jvalue* args, char* argTypes) {
    for (int i=0; i<nargs; i++) {
        int idx = startIdx + i;
        switch (argTypes[i]) {
            case 'I': args[i].i = (jint)lua_tointeger(L, idx); break;
            case 'D': args[i].d = (jdouble)lua_tonumber(L, idx); break;
            case 'Z': args[i].z = (jboolean)lua_toboolean(L, idx); break;
            case 'S': args[i].l = lua_check_jstring(env, L, idx); break;
            default:  args[i].l = NULL; break;
        }
    }
}

static int push_java_result(lua_State* L, JNIEnv* env, jvalue result, char returnType) {
    switch (returnType) {
        case 'V': lua_pushnil(L); return 1;
        case 'I': lua_pushinteger(L, (lua_Integer)result.i); return 1;
        case 'D': lua_pushnumber(L, (lua_Number)result.d); return 1;
        case 'Z': lua_pushboolean(L, result.z); return 1;
        case 'J': lua_pushinteger(L, (lua_Integer)result.j); return 1;
        case 'S': {
            if (!result.l) { lua_pushnil(L); }
            else { const char* s = (*env)->GetStringUTFChars(env, (jstring)result.l, NULL);
                   lua_pushstring(L, s); (*env)->ReleaseStringUTFChars(env, (jstring)result.l, s); }
            return 1;
        }
        default:
            if (!result.l) { lua_pushnil(L); }
            else { new_java_object_ud(L, result.l); }
            return 1;
    }
}

// ========== Java.Array 元方法 ==========
static int java_array_index(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaArray* arr = (JavaArray*)luaL_checkudata(L, 1, JAVAARRAY_META);
    int idx = (int)luaL_checkinteger(L, 2);

    if (idx < 0 || idx >= arr->length) {
        return luaL_error(L, "array index out of bounds: %d (size %d)", idx, arr->length);
    }

    jclass arrayCls = (*env)->FindClass(env, "java/lang/reflect/Array");
    jmethodID getMid = (*env)->GetStaticMethodID(env, arrayCls, "get", "(Ljava/lang/Object;I)Ljava/lang/Object;");
    jobject val = (*env)->CallStaticObjectMethod(env, arrayCls, getMid, arr->arrayObj, idx);
    (*env)->DeleteLocalRef(env, arrayCls);

    if (!val) { lua_pushnil(L); return 1; }

    switch (arr->elementType) {
        case 'I': {
            jclass intCls = (*env)->GetObjectClass(env, val);
            jmethodID intVal = (*env)->GetMethodID(env, intCls, "intValue", "()I");
            lua_pushinteger(L, (*env)->CallIntMethod(env, val, intVal));
            (*env)->DeleteLocalRef(env, intCls);
            break;
        }
        case 'D': {
            jclass dblCls = (*env)->GetObjectClass(env, val);
            jmethodID dblVal = (*env)->GetMethodID(env, dblCls, "doubleValue", "()D");
            lua_pushnumber(L, (*env)->CallDoubleMethod(env, val, dblVal));
            (*env)->DeleteLocalRef(env, dblCls);
            break;
        }
        case 'Z': {
            jclass boolCls = (*env)->GetObjectClass(env, val);
            jmethodID boolVal = (*env)->GetMethodID(env, boolCls, "booleanValue", "()Z");
            lua_pushboolean(L, (*env)->CallBooleanMethod(env, val, boolVal));
            (*env)->DeleteLocalRef(env, boolCls);
            break;
        }
        case 'S': {
            const char* s = (*env)->GetStringUTFChars(env, (jstring)val, NULL);
            lua_pushstring(L, s);
            (*env)->ReleaseStringUTFChars(env, (jstring)val, s);
            break;
        }
        default:
            new_java_object_ud(L, val);
            break;
    }
    (*env)->DeleteLocalRef(env, val);
    return 1;
}

static int java_array_newindex(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaArray* arr = (JavaArray*)luaL_checkudata(L, 1, JAVAARRAY_META);
    int idx = (int)luaL_checkinteger(L, 2);

    if (idx < 0 || idx >= arr->length) {
        lua_pushfstring(L, "array index out of bounds: %d (size %d)", idx, arr->length);
        return lua_error(L);
    }

    jclass arrayCls = (*env)->FindClass(env, "java/lang/reflect/Array");

    switch (arr->elementType) {
        case 'I': {
            jint val = (jint)luaL_checkinteger(L, 3);
            jmethodID setMid = (*env)->GetStaticMethodID(env, arrayCls, "setInt", "(Ljava/lang/Object;II)V");
            (*env)->CallStaticVoidMethod(env, arrayCls, setMid, arr->arrayObj, idx, val);
            break;
        }
        case 'D': {
            jdouble val = (jdouble)luaL_checknumber(L, 3);
            jmethodID setMid = (*env)->GetStaticMethodID(env, arrayCls, "setDouble", "(Ljava/lang/Object;ID)V");
            (*env)->CallStaticVoidMethod(env, arrayCls, setMid, arr->arrayObj, idx, val);
            break;
        }
        case 'Z': {
            jboolean val = (jboolean)lua_toboolean(L, 3);
            jmethodID setMid = (*env)->GetStaticMethodID(env, arrayCls, "setBoolean", "(Ljava/lang/Object;IZ)V");
            (*env)->CallStaticVoidMethod(env, arrayCls, setMid, arr->arrayObj, idx, val);
            break;
        }
        case 'S': {
            jstring val = lua_check_jstring(env, L, 3);
            jmethodID setMid = (*env)->GetStaticMethodID(env, arrayCls, "set", "(Ljava/lang/Object;ILjava/lang/Object;)V");
            (*env)->CallStaticVoidMethod(env, arrayCls, setMid, arr->arrayObj, idx, val);
            (*env)->DeleteLocalRef(env, val);
            break;
        }
        default: {
            JavaUserdata* ud = (JavaUserdata*)luaL_testudata(L, 3, JAVAOBJECT_META);
            if (ud) {
                jmethodID setMid = (*env)->GetStaticMethodID(env, arrayCls, "set", "(Ljava/lang/Object;ILjava/lang/Object;)V");
                (*env)->CallStaticVoidMethod(env, arrayCls, setMid, arr->arrayObj, idx, ud->obj);
            }
            break;
        }
    }
    (*env)->DeleteLocalRef(env, arrayCls);
    return 0;
}

static int java_array_len(lua_State* L) {
    JavaArray* arr = (JavaArray*)luaL_checkudata(L, 1, JAVAARRAY_META);
    lua_pushinteger(L, arr->length);
    return 1;
}

static int java_array_tostring(lua_State* L) {
    JavaArray* arr = (JavaArray*)luaL_checkudata(L, 1, JAVAARRAY_META);
    const char* typeStr = "Object";
    switch (arr->elementType) {
        case 'I': typeStr = "int"; break;
        case 'D': typeStr = "double"; break;
        case 'Z': typeStr = "boolean"; break;
        case 'S': typeStr = "String"; break;
    }
    lua_pushfstring(L, "Java.Array[%s](%d)", typeStr, arr->length);
    return 1;
}

static int java_array_gc(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaArray* arr = (JavaArray*)luaL_checkudata(L, 1, JAVAARRAY_META);
    if (arr->arrayObj) { (*env)->DeleteGlobalRef(env, arr->arrayObj); arr->arrayObj = NULL; }
    if (arr->elementClass) { (*env)->DeleteWeakGlobalRef(env, arr->elementClass); arr->elementClass = NULL; }
    return 0;
}

// ========== MethodLookup 元方法 ==========
static int method_lookup_call(lua_State* L) {
    JNIEnv* env = getEnv();
    MethodLookup* ml = (MethodLookup*)luaL_checkudata(L, 1, METHODLOOKUP_META);
    int firstArgIdx = 2;
    int nargs = lua_gettop(L) - 1;
    if (!ml->isStatic && nargs >= 1 && lua_isuserdata(L, firstArgIdx)) {
        // 检查是否是 Java.Object 且是同一个对象
        if (lua_getmetatable(L, firstArgIdx)) {
            luaL_getmetatable(L, JAVAOBJECT_META);
            int isObj = lua_rawequal(L, -1, -2);
            lua_pop(L, 2);
            if (isObj) {
                JavaUserdata* ud = (JavaUserdata*)lua_touserdata(L, firstArgIdx);
                if (ud && (*env)->IsSameObject(env, ud->obj, ml->obj)) {
                    firstArgIdx = 3;
                    nargs = lua_gettop(L) - 2;
                }
            }
        }
    }
    jclass cls = ml->isStatic ? (jclass)ml->obj : (*env)->GetObjectClass(env, ml->obj);
    char returnType = 'O';
    jmethodID method = try_find_method(env, cls, ml->methodName, L, firstArgIdx, nargs, &returnType, ml->isStatic);
    if (!method) {
        if (!ml->isStatic) (*env)->DeleteLocalRef(env, cls);
        return luaL_error(L, "method not found: %s", ml->methodName);
        return 2;
    }
    char argTypes[16];
    for (int i=0; i<nargs; i++) argTypes[i] = get_java_type_char(L, firstArgIdx+i);
    jvalue args[nargs]; memset(args, 0, sizeof(args));
    push_jni_args(L, env, firstArgIdx, nargs, args, argTypes);
    jvalue result; memset(&result, 0, sizeof(result));
    if (ml->isStatic) {
        switch (returnType) {
            case 'S': result.l=(*env)->CallStaticObjectMethodA(env, cls, method, args); break;
            case 'I': result.i=(*env)->CallStaticIntMethodA(env, cls, method, args); break;
            case 'D': result.d=(*env)->CallStaticDoubleMethodA(env, cls, method, args); break;
            case 'Z': result.z=(*env)->CallStaticBooleanMethodA(env, cls, method, args); break;
            case 'V': (*env)->CallStaticVoidMethodA(env, cls, method, args); break;
            case 'J': result.j=(*env)->CallStaticLongMethodA(env, cls, method, args); break;
            default:  result.l=(*env)->CallStaticObjectMethodA(env, cls, method, args); break;
        }
    } else {
        switch (returnType) {
            case 'S': result.l=(*env)->CallObjectMethodA(env, ml->obj, method, args); break;
            case 'I': result.i=(*env)->CallIntMethodA(env, ml->obj, method, args); break;
            case 'D': result.d=(*env)->CallDoubleMethodA(env, ml->obj, method, args); break;
            case 'Z': result.z=(*env)->CallBooleanMethodA(env, ml->obj, method, args); break;
            case 'V': (*env)->CallVoidMethodA(env, ml->obj, method, args); break;
            default:  result.l=(*env)->CallObjectMethodA(env, ml->obj, method, args); break;
        }
    }
    for (int i=0; i<nargs; i++) if (argTypes[i]=='S' && args[i].l) (*env)->DeleteLocalRef(env, args[i].l);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env);
        if (!ml->isStatic) (*env)->DeleteLocalRef(env, cls);
        lua_pushnil(L); lua_pushfstring(L, "method threw exception: %s", ml->methodName);
        return 2;
    }
    if (!ml->isStatic) (*env)->DeleteLocalRef(env, cls);
    return push_java_result(L, env, result, returnType);
}

static int method_lookup_gc(lua_State* L) {
    JNIEnv* env = getEnv();
    MethodLookup* ml = (MethodLookup*)luaL_checkudata(L, 1, METHODLOOKUP_META);
    if (ml->obj) (*env)->DeleteGlobalRef(env, ml->obj);
    free(ml->methodName);
    return 0;
}

// ========== Java.Class 元方法 ==========
static int java_class_tostring(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVACLASS_META);
    char* name = get_class_name_from_classobj(env, (jclass)ud->obj);
    lua_pushfstring(L, "Java.Class[%s]", name);
    free(name);
    return 1;
}

static int java_class_call(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVACLASS_META);
    jclass cls = (jclass)ud->obj;
    int nargs = lua_gettop(L) - 1;
    char returnType = 'V';
    char sig[128] = "(";
    jmethodID ctor = try_method_combinations(env, cls, "<init>", L, 2, nargs, returnType, 0, sig, &returnType, 0);
    if (!ctor) {
        if (nargs == 1 && lua_isuserdata(L, 2)) {
            JavaUserdata* argUd = (JavaUserdata*)luaL_testudata(L, 2, JAVAOBJECT_META);
            if (argUd) {
                jclass argCls = (*env)->GetObjectClass(env, argUd->obj);
                ctor = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/Runnable;)V");
                if (!ctor || (*env)->ExceptionCheck(env)) {
                    (*env)->ExceptionClear(env);
                    ctor = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/Object;)V");
                }
                (*env)->DeleteLocalRef(env, argCls);
            }
        }
    }
    if (!ctor || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return luaL_error(L, "constructor not found");
    }
    char argTypes[16];
    for (int i=0; i<nargs; i++) argTypes[i] = get_java_type_char(L, 2+i);
    jvalue args[nargs]; memset(args, 0, sizeof(args));
    push_jni_args(L, env, 2, nargs, args, argTypes);
    jobject obj = (*env)->NewObjectA(env, cls, ctor, args);
    for (int i=0; i<nargs; i++) if (argTypes[i]=='S' && args[i].l) (*env)->DeleteLocalRef(env, args[i].l);
    if (!obj || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        lua_pushnil(L); lua_pushstring(L, "failed to create object"); return 2;
    }
    new_java_object_ud(L, obj);
    (*env)->DeleteLocalRef(env, obj);
    return 1;
}

static int java_class_index(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVACLASS_META);
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "new") == 0) { lua_pushcfunction(L, java_class_call); return 1; }
    jclass cls = (jclass)ud->obj;
    jfieldID field = (*env)->GetStaticFieldID(env, cls, key, "Ljava/lang/String;");
    if (field && !(*env)->ExceptionCheck(env)) {
        jstring val = (jstring)(*env)->GetStaticObjectField(env, cls, field);
        if (val) { const char* c = (*env)->GetStringUTFChars(env, val, NULL); lua_pushstring(L, c); (*env)->ReleaseStringUTFChars(env, val, c); (*env)->DeleteLocalRef(env, val); return 1; }
        lua_pushnil(L); return 1;
    }
    (*env)->ExceptionClear(env);
    field = (*env)->GetStaticFieldID(env, cls, key, "I");
    if (field && !(*env)->ExceptionCheck(env)) { lua_pushinteger(L, (*env)->GetStaticIntField(env, cls, field)); return 1; }
    (*env)->ExceptionClear(env);
    field = (*env)->GetStaticFieldID(env, cls, key, "Z");
    if (field && !(*env)->ExceptionCheck(env)) { lua_pushboolean(L, (*env)->GetStaticBooleanField(env, cls, field)); return 1; }
    (*env)->ExceptionClear(env);
    field = (*env)->GetStaticFieldID(env, cls, key, "D");
    if (field && !(*env)->ExceptionCheck(env)) { lua_pushnumber(L, (*env)->GetStaticDoubleField(env, cls, field)); return 1; }
    (*env)->ExceptionClear(env);
    return new_method_lookup(L, ud->obj, key, 1);
}

static int java_class_newindex(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVACLASS_META);
    const char* key = luaL_checkstring(L, 2);
    jclass cls = (jclass)ud->obj;
    const char* sig = lua_type_to_jni_sig(L, 3);
    jfieldID field = (*env)->GetStaticFieldID(env, cls, key, sig);
    if (!field || (*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); lua_pushfstring(L, "static field not found: %s", key); return lua_error(L); }
    switch (lua_type(L, 3)) {
        case LUA_TNUMBER: if (lua_isinteger(L,3)) (*env)->SetStaticIntField(env, cls, field, (jint)lua_tointeger(L,3)); else (*env)->SetStaticDoubleField(env, cls, field, (jdouble)lua_tonumber(L,3)); break;
        case LUA_TSTRING: { jstring s = lua_check_jstring(env, L, 3); (*env)->SetStaticObjectField(env, cls, field, s); (*env)->DeleteLocalRef(env, s); break; }
        case LUA_TBOOLEAN: (*env)->SetStaticBooleanField(env, cls, field, (jboolean)lua_toboolean(L,3)); break;
    }
    return 0;
}

// ========== Java.Object 元方法 ==========
static int java_object_tostring(lua_State* L) {
    lua_pushstring(L, "Java.Object");
    return 1;
}

static int java_object_index(lua_State* L) {
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVAOBJECT_META);
    const char* key = luaL_checkstring(L, 2);
    JNIEnv* env = getEnv();
    jclass cls = (*env)->GetObjectClass(env, ud->obj);
    jfieldID field = (*env)->GetFieldID(env, cls, key, "Ljava/lang/String;");
    if (field && !(*env)->ExceptionCheck(env)) {
        jstring val = (jstring)(*env)->GetObjectField(env, ud->obj, field);
        (*env)->DeleteLocalRef(env, cls);
        if (val) { const char* c = (*env)->GetStringUTFChars(env, val, NULL); lua_pushstring(L, c); (*env)->ReleaseStringUTFChars(env, val, c); (*env)->DeleteLocalRef(env, val); return 1; }
        lua_pushnil(L); return 1;
    }
    (*env)->ExceptionClear(env);
    field = (*env)->GetFieldID(env, cls, key, "I");
    if (field && !(*env)->ExceptionCheck(env)) { lua_pushinteger(L, (*env)->GetIntField(env, ud->obj, field)); (*env)->DeleteLocalRef(env, cls); return 1; }
    (*env)->ExceptionClear(env);
    field = (*env)->GetFieldID(env, cls, key, "Z");
    if (field && !(*env)->ExceptionCheck(env)) { lua_pushboolean(L, (*env)->GetBooleanField(env, ud->obj, field)); (*env)->DeleteLocalRef(env, cls); return 1; }
    (*env)->ExceptionClear(env);
    field = (*env)->GetFieldID(env, cls, key, "D");
    if (field && !(*env)->ExceptionCheck(env)) { lua_pushnumber(L, (*env)->GetDoubleField(env, ud->obj, field)); (*env)->DeleteLocalRef(env, cls); return 1; }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    return new_method_lookup(L, ud->obj, key, 0);
}

static int java_object_newindex(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVAOBJECT_META);
    const char* key = luaL_checkstring(L, 2);
    jclass cls = (*env)->GetObjectClass(env, ud->obj);
    const char* sig = lua_type_to_jni_sig(L, 3);
    jfieldID field = (*env)->GetFieldID(env, cls, key, sig);
    if (!field || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*env)->DeleteLocalRef(env, cls);
        lua_pushfstring(L, "field not found: %s", key); return lua_error(L);
    }
    switch (lua_type(L, 3)) {
        case LUA_TNUMBER: if (lua_isinteger(L,3)) (*env)->SetIntField(env, ud->obj, field, (jint)lua_tointeger(L,3)); else (*env)->SetDoubleField(env, ud->obj, field, (jdouble)lua_tonumber(L,3)); break;
        case LUA_TSTRING: { jstring s = lua_check_jstring(env, L, 3); (*env)->SetObjectField(env, ud->obj, field, s); (*env)->DeleteLocalRef(env, s); break; }
        case LUA_TBOOLEAN: (*env)->SetBooleanField(env, ud->obj, field, (jboolean)lua_toboolean(L,3)); break;
        default: { JavaUserdata* vud = (JavaUserdata*)luaL_testudata(L, 3, JAVAOBJECT_META); if (vud && vud->obj) (*env)->SetObjectField(env, ud->obj, field, vud->obj); break; }
    }
    (*env)->DeleteLocalRef(env, cls);
    return 0;
}

static int java_object_gc(lua_State* L) {
    JNIEnv* env = getEnv();
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVAOBJECT_META);
    if (ud->obj) { (*env)->DeleteGlobalRef(env, ud->obj); ud->obj = NULL; }
    if (ud->cls) { (*env)->DeleteWeakGlobalRef(env, ud->cls); ud->cls = NULL; }
    return 0;
}

// ========== java.createProxy ==========
static int java_createProxy(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    JNIEnv* env = getEnv();

    // 获取 statePtr
    lua_pushstring(L, "luajava_stateptr");
    lua_rawget(L, LUA_REGISTRYINDEX);
    jlong statePtr = (jlong)lua_tointeger(L, -1);
    lua_pop(L, 1);

    // 读取接口名数组
    int nInterfaces = luaL_len(L, 1);

    jclass clsCls = (*env)->FindClass(env, "java/lang/Class");

    jobjectArray ifaceArray = (*env)->NewObjectArray(env, nInterfaces, clsCls, NULL);
    (*env)->DeleteLocalRef(env, clsCls);

    for (int i = 0; i < nInterfaces; i++) {
        lua_rawgeti(L, 1, i + 1);
        const char* name = lua_tostring(L, -1);

        char* desc = strdup(name);
        for (char* p = desc; *p; p++) if (*p == '.') *p = '/';
        jclass iface = (*env)->FindClass(env, desc);
        free(desc);
        lua_pop(L, 1);

        if (!iface || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteLocalRef(env, ifaceArray);
            lua_pushnil(L);
            lua_pushstring(L, "interface not found");
            return 2;
        }
        (*env)->SetObjectArrayElement(env, ifaceArray, i, iface);
        (*env)->DeleteLocalRef(env, iface);
    }

    // 保存 Lua 表到注册表
    lua_pushvalue(L, 2);
    int tableRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // 创建 LuaInvocationHandler
    jclass handlerCls = (*env)->FindClass(env, "com/luajava/LuaInvocationHandler");

    jmethodID handlerCtor = (*env)->GetMethodID(env, handlerCls, "<init>", "(JI)V");

    jobject handler = (*env)->NewObject(env, handlerCls, handlerCtor, statePtr, (jint)tableRef);
    (*env)->DeleteLocalRef(env, handlerCls);

    // Proxy.newProxyInstance
    jclass proxyCls = (*env)->FindClass(env, "java/lang/reflect/Proxy");

    jmethodID newProxy = (*env)->GetStaticMethodID(env, proxyCls,
        "newProxyInstance",
        "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;");

    // 获取类加载器
    jobject firstIface = (*env)->GetObjectArrayElement(env, ifaceArray, 0);

    jclass ifaceCls = (*env)->GetObjectClass(env, firstIface);

    jmethodID getClassLoader = (*env)->GetMethodID(env, ifaceCls, "getClassLoader", "()Ljava/lang/ClassLoader;");

    jobject classLoader = (*env)->CallObjectMethod(env, firstIface, getClassLoader);

    (*env)->DeleteLocalRef(env, ifaceCls);
    (*env)->DeleteLocalRef(env, firstIface);

    // 调用 newProxyInstance
    jobject proxy = (*env)->CallStaticObjectMethod(env, proxyCls, newProxy,
        classLoader, ifaceArray, handler);

    (*env)->DeleteLocalRef(env, classLoader);
    (*env)->DeleteLocalRef(env, ifaceArray);
    (*env)->DeleteLocalRef(env, handler);
    (*env)->DeleteLocalRef(env, proxyCls);

    if (!proxy || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        luaL_unref(L, LUA_REGISTRYINDEX, tableRef);
        lua_pushnil(L);
        lua_pushstring(L, "failed to create proxy");
        return 2;
    }

    new_java_object_ud(L, proxy);
    (*env)->DeleteLocalRef(env, proxy);
    return 1;
}

// ========== java.newArray ==========
static int java_newArray(lua_State* L) {
    const char* typeName = luaL_checkstring(L, 1);
    int size = (int)luaL_checkinteger(L, 2);
    JNIEnv* env = getEnv();

    JavaArray* arr = (JavaArray*)lua_newuserdatauv(L, sizeof(JavaArray), 0);
    arr->length = size;

    if (strcmp(typeName, "int") == 0 || strcmp(typeName, "java.lang.Integer") == 0) {
        jclass intCls = (*env)->FindClass(env, "java/lang/Integer");
        jfieldID typeField = (*env)->GetStaticFieldID(env, intCls, "TYPE", "Ljava/lang/Class;");
        jclass intTypeCls = (jclass)(*env)->GetStaticObjectField(env, intCls, typeField);
        jclass arrCls = (*env)->FindClass(env, "java/lang/reflect/Array");
        jmethodID newArr = (*env)->GetStaticMethodID(env, arrCls, "newInstance",
            "(Ljava/lang/Class;I)Ljava/lang/Object;");
        jobject arrayObj = (*env)->CallStaticObjectMethod(env, arrCls, newArr, intTypeCls, size);
        arr->arrayObj = (*env)->NewGlobalRef(env, arrayObj);
        arr->elementType = 'I';
        arr->elementClass = (*env)->NewWeakGlobalRef(env, intTypeCls);
        (*env)->DeleteLocalRef(env, arrayObj);
        (*env)->DeleteLocalRef(env, arrCls);
        (*env)->DeleteLocalRef(env, intTypeCls);
        (*env)->DeleteLocalRef(env, intCls);
    } else if (strcmp(typeName, "double") == 0 || strcmp(typeName, "java.lang.Double") == 0) {
        jclass dblCls = (*env)->FindClass(env, "java/lang/Double");
        jfieldID typeField = (*env)->GetStaticFieldID(env, dblCls, "TYPE", "Ljava/lang/Class;");
        jclass dblTypeCls = (jclass)(*env)->GetStaticObjectField(env, dblCls, typeField);
        jclass arrCls = (*env)->FindClass(env, "java/lang/reflect/Array");
        jmethodID newArr = (*env)->GetStaticMethodID(env, arrCls, "newInstance",
            "(Ljava/lang/Class;I)Ljava/lang/Object;");
        jobject arrayObj = (*env)->CallStaticObjectMethod(env, arrCls, newArr, dblTypeCls, size);
        arr->arrayObj = (*env)->NewGlobalRef(env, arrayObj);
        arr->elementType = 'D';
        arr->elementClass = (*env)->NewWeakGlobalRef(env, dblTypeCls);
        (*env)->DeleteLocalRef(env, arrayObj);
        (*env)->DeleteLocalRef(env, arrCls);
        (*env)->DeleteLocalRef(env, dblTypeCls);
        (*env)->DeleteLocalRef(env, dblCls);
    } else if (strcmp(typeName, "boolean") == 0 || strcmp(typeName, "java.lang.Boolean") == 0) {
        jclass boolCls = (*env)->FindClass(env, "java/lang/Boolean");
        jfieldID typeField = (*env)->GetStaticFieldID(env, boolCls, "TYPE", "Ljava/lang/Class;");
        jclass boolTypeCls = (jclass)(*env)->GetStaticObjectField(env, boolCls, typeField);
        jclass arrCls = (*env)->FindClass(env, "java/lang/reflect/Array");
        jmethodID newArr = (*env)->GetStaticMethodID(env, arrCls, "newInstance",
            "(Ljava/lang/Class;I)Ljava/lang/Object;");
        jobject arrayObj = (*env)->CallStaticObjectMethod(env, arrCls, newArr, boolTypeCls, size);
        arr->arrayObj = (*env)->NewGlobalRef(env, arrayObj);
        arr->elementType = 'Z';
        arr->elementClass = (*env)->NewWeakGlobalRef(env, boolTypeCls);
        (*env)->DeleteLocalRef(env, arrayObj);
        (*env)->DeleteLocalRef(env, arrCls);
        (*env)->DeleteLocalRef(env, boolTypeCls);
        (*env)->DeleteLocalRef(env, boolCls);
    } else if (strcmp(typeName, "String") == 0 || strcmp(typeName, "java.lang.String") == 0) {
        jclass strCls = (*env)->FindClass(env, "java/lang/String");
        jclass arrCls = (*env)->FindClass(env, "java/lang/reflect/Array");
        jmethodID newArr = (*env)->GetStaticMethodID(env, arrCls, "newInstance",
            "(Ljava/lang/Class;I)Ljava/lang/Object;");
        jobject arrayObj = (*env)->CallStaticObjectMethod(env, arrCls, newArr, strCls, size);
        arr->arrayObj = (*env)->NewGlobalRef(env, arrayObj);
        arr->elementType = 'S';
        arr->elementClass = (*env)->NewWeakGlobalRef(env, strCls);
        (*env)->DeleteLocalRef(env, arrayObj);
        (*env)->DeleteLocalRef(env, arrCls);
        (*env)->DeleteLocalRef(env, strCls);
    } else {
        lua_pushnil(L);
        lua_pushfstring(L, "unsupported array type: %s", typeName);
        return 2;
    }

    luaL_getmetatable(L, JAVAARRAY_META);
    lua_setmetatable(L, -2);
    return 1;
}

// ========== 库函数 ==========
static int java_import(lua_State* L) {
    const char* className = luaL_checkstring(L, 1);
    JNIEnv* env = getEnv();
    char* desc = strdup(className);
    for (char* p = desc; *p; p++) if (*p == '.') *p = '/';
    jclass cls = (*env)->FindClass(env, desc);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); free(desc);
        return luaL_error(L, "class not found: %s", className);
        return 2;
    }
    free(desc);
    new_java_class_ud(L, cls);
    (*env)->DeleteLocalRef(env, cls);
    return 1;
}

static int java_toString(lua_State* L) {
    return java_object_tostring(L);
}

// ========== 创建元表 ==========
static void create_metatables(lua_State* L) {
    luaL_newmetatable(L, JAVACLASS_META);
    lua_pushstring(L, "__call");     lua_pushcfunction(L, java_class_call);     lua_settable(L, -3);
    lua_pushstring(L, "__index");    lua_pushcfunction(L, java_class_index);    lua_settable(L, -3);
    lua_pushstring(L, "__newindex"); lua_pushcfunction(L, java_class_newindex); lua_settable(L, -3);
    lua_pushstring(L, "__tostring"); lua_pushcfunction(L, java_class_tostring); lua_settable(L, -3);
    lua_pop(L, 1);

    luaL_newmetatable(L, JAVAOBJECT_META);
    lua_pushstring(L, "__index");    lua_pushcfunction(L, java_object_index);    lua_settable(L, -3);
    lua_pushstring(L, "__newindex"); lua_pushcfunction(L, java_object_newindex); lua_settable(L, -3);
    lua_pushstring(L, "__tostring"); lua_pushcfunction(L, java_object_tostring); lua_settable(L, -3);
    lua_pushstring(L, "__gc");       lua_pushcfunction(L, java_object_gc);       lua_settable(L, -3);
    lua_pop(L, 1);

    luaL_newmetatable(L, METHODLOOKUP_META);
    lua_pushstring(L, "__call"); lua_pushcfunction(L, method_lookup_call); lua_settable(L, -3);
    lua_pushstring(L, "__gc");   lua_pushcfunction(L, method_lookup_gc);   lua_settable(L, -3);
    lua_pop(L, 1);

    luaL_newmetatable(L, JAVAARRAY_META);
    lua_pushstring(L, "__index");    lua_pushcfunction(L, java_array_index);    lua_settable(L, -3);
    lua_pushstring(L, "__newindex"); lua_pushcfunction(L, java_array_newindex); lua_settable(L, -3);
    lua_pushstring(L, "__len");      lua_pushcfunction(L, java_array_len);      lua_settable(L, -3);
    lua_pushstring(L, "__tostring"); lua_pushcfunction(L, java_array_tostring); lua_settable(L, -3);
    lua_pushstring(L, "__gc");       lua_pushcfunction(L, java_array_gc);       lua_settable(L, -3);
    lua_pop(L, 1);
}

// ========== java.promise() ==========
// 创建一个 Promise 对象，可用于 java.await() 等待
static int java_promise(lua_State* L) {
    JNIEnv* env = getEnv();

    // 获取 statePtr
    lua_pushstring(L, "luajava_stateptr");
    lua_rawget(L, LUA_REGISTRYINDEX);
    jlong statePtr = (jlong)lua_tointeger(L, -1);
    lua_pop(L, 1);

    // threadRef 设为 -1，等 java.await 时再设置
    int threadRef = -1;

    // 创建 LuaPromise Java 对象
    jclass promiseCls = (*env)->FindClass(env, "com/luajava/LuaPromise");
    jmethodID ctor = (*env)->GetMethodID(env, promiseCls, "<init>", "(JI)V");
    jobject promise = (*env)->NewObject(env, promiseCls, ctor, statePtr, (jint)threadRef);
    (*env)->DeleteLocalRef(env, promiseCls);

    // 包装成 userdata 返回给 Lua
    extern int new_java_object_ud(lua_State* L, jobject obj);
    new_java_object_ud(L, promise);
    (*env)->DeleteLocalRef(env, promise);
    return 1;
}

// ========== java.await(promise) ==========
// 挂起当前协程，等待 Promise 被 complete
static int java_await(lua_State* L) {
    JNIEnv* env = getEnv();

    // 参数：JavaObject (LuaPromise)
    JavaUserdata* ud = (JavaUserdata*)luaL_testudata(L, 1, JAVAOBJECT_META);
    if (!ud || !ud->obj) {
        return luaL_error(L, "java.await: expected a LuaPromise");
    }

    // 把当前协程的引用保存到注册表，并更新 Promise 的 threadRef
    lua_pushthread(L);
    int threadRef = luaL_ref(L, LUA_REGISTRYINDEX);

    jclass cls = (*env)->GetObjectClass(env, ud->obj);
    jfieldID refField = (*env)->GetFieldID(env, cls, "threadRef", "I");
    (*env)->SetIntField(env, ud->obj, refField, threadRef);
    (*env)->DeleteLocalRef(env, cls);

    // 挂起当前协程
    return lua_yield(L, 0);
}

static const luaL_Reg javalib[] = {
    {"import",      java_import},
    {"toString",    java_toString},
    {"promise",     java_promise},
    {"await",       java_await},
    {"createProxy", java_createProxy},
    {"newArray",    java_newArray},
    {NULL, NULL}
};

int luaopen_java(lua_State* L) {
    create_metatables(L);
    luaL_newlib(L, javalib);
    return 1;
}