#include "lualibjava_internal.h"
// Lua5.4.8/lualibjava.c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <pthread.h>
#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>


extern JNIEnv* getEnv();
extern int java_runAsync(lua_State* L);
extern int java_getObject(lua_State* L);
extern int java_runAsyncObj(lua_State* L);
extern int java_checkPromise(lua_State* L);
extern void dispatch_callback(lua_State* owner, int cbRef, const char* result);



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
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                lua_Integer n = lua_tointeger(L, idx);
                if (n > 2147483647 || n < -2147483648) return 'J';
                return 'I';
            }
            return 'D';
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

// ========== Java 数组包装 ==========

// 由数组的组件类型 Class 解析元素类型代码：
//   I/J/B/S = int/long/byte/short，D/F = double/float，Z = boolean，C = char，
//   T = String，A = 嵌套数组（多维），O = 其他对象
static char array_element_type_from_class(JNIEnv* env, jclass compCls) {
    if (!compCls) return 'O';
    jclass cCls = (*env)->GetObjectClass(env, compCls);
    jmethodID getName = (*env)->GetMethodID(env, cCls, "getName", "()Ljava/lang/String;");
    jstring jname = (jstring)(*env)->CallObjectMethod(env, compCls, getName);
    const char* name = (*env)->GetStringUTFChars(env, jname, NULL);
    char t = 'O';
    if      (!strcmp(name, "int"))     t = 'I';
    else if (!strcmp(name, "long"))    t = 'J';
    else if (!strcmp(name, "byte"))    t = 'B';
    else if (!strcmp(name, "short"))   t = 'S';
    else if (!strcmp(name, "double"))  t = 'D';
    else if (!strcmp(name, "float"))   t = 'F';
    else if (!strcmp(name, "boolean")) t = 'Z';
    else if (!strcmp(name, "char"))    t = 'C';
    else if (!strcmp(name, "java.lang.String")) t = 'S';
    else if (name[0] == '[')           t = 'A';
    (*env)->ReleaseStringUTFChars(env, jname, name);
    (*env)->DeleteLocalRef(env, jname);
    (*env)->DeleteLocalRef(env, cCls);
    return t;
}

// 将已有 Java 数组对象包装为 Java.Array userdata（与 java.newArray 相同的元表与元素语义）。
// arrCls 可传 NULL（内部自行获取）；传入非 NULL 时由本函数负责释放该局部引用。
static JavaArray* new_java_array_ud(lua_State* L, JNIEnv* env, jobject arrayObj, jclass arrCls) {
    jclass cls = arrCls ? arrCls : (*env)->GetObjectClass(env, arrayObj);
    jclass cCls = (*env)->GetObjectClass(env, cls);
    jmethodID getComp = (*env)->GetMethodID(env, cCls, "getComponentType", "()Ljava/lang/Class;");
    jclass compCls = (jclass)(*env)->CallObjectMethod(env, cls, getComp);
    char etype = array_element_type_from_class(env, compCls);

    JavaArray* arr = (JavaArray*)lua_newuserdatauv(L, sizeof(JavaArray), 0);
    arr->arrayObj     = (*env)->NewGlobalRef(env, arrayObj);
    arr->elementClass = compCls ? (jclass)(*env)->NewWeakGlobalRef(env, compCls) : NULL;
    arr->elementType  = etype;
    arr->length       = (*env)->GetArrayLength(env, arrayObj);

    luaL_getmetatable(L, JAVAARRAY_META);
    lua_setmetatable(L, -2);

    (*env)->DeleteLocalRef(env, compCls);
    (*env)->DeleteLocalRef(env, cCls);
    (*env)->DeleteLocalRef(env, cls);
    return arr;
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
        // 统一包装为 Java.Array userdata，与 java.newArray 完全一致
        // （new_java_array_ud 内部会释放传入的 objCls 局部引用）
        new_java_array_ud(L, env, obj, objCls);
        return 1;
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

// ========== 反射工具：类型映射与装箱 ==========

// Java Class -> JNI 类型字符。约定：String 引用返回 'S'，short 基本类型返回 't'，
// byte 返回 'B'，char 返回 'C'，其余引用类型返回 'L'。
static char java_class_to_type_char(JNIEnv* env, jclass cls) {
    jclass classCls = (*env)->GetObjectClass(env, cls);
    jmethodID isPrim = (*env)->GetMethodID(env, classCls, "isPrimitive", "()Z");
    jmethodID getName = (*env)->GetMethodID(env, classCls, "getName", "()Ljava/lang/String;");
    if ((*env)->CallBooleanMethod(env, cls, isPrim)) {
        jstring nm = (jstring)(*env)->CallObjectMethod(env, cls, getName);
        const char* c = (*env)->GetStringUTFChars(env, nm, NULL);
        char r;
        switch (c[0]) {
            case 'v': r = 'V'; break;
            case 'i': r = 'I'; break;
            case 'l': r = 'J'; break;
            case 'd': r = 'D'; break;
            case 'f': r = 'F'; break;
            case 'b': r = (c[1] == 'o') ? 'Z' : 'B'; break;
            case 'c': r = 'C'; break;
            case 's': r = 't'; break; // short 与 String 区分
            default:  r = 'O';
        }
        (*env)->ReleaseStringUTFChars(env, nm, c);
        (*env)->DeleteLocalRef(env, nm);
        (*env)->DeleteLocalRef(env, classCls);
        return r;
    }
    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jboolean isStr = (*env)->IsInstanceOf(env, cls, strCls);
    (*env)->DeleteLocalRef(env, strCls);
    (*env)->DeleteLocalRef(env, classCls);
    return isStr ? 'S' : 'L';
}

// Lua 参数与 Java 参数类型是否兼容
static int lua_compat_with_class(JNIEnv* env, lua_State* L, int idx, jclass pcls) {
    char t = java_class_to_type_char(env, pcls);
    int lt = lua_type(L, idx);
    switch (t) {
        case 'I': case 'J': case 'D': case 'F': case 't': case 'B':
            return lt == LUA_TNUMBER;
        case 'Z':
            return lt == LUA_TBOOLEAN;
        case 'C':
            return lt == LUA_TSTRING || lt == LUA_TNUMBER;
        case 'S':
            return lt == LUA_TSTRING;
        default: // 'L' 引用类型：userdata / 任意可装箱标量
            return lt == LUA_TUSERDATA || lt == LUA_TSTRING ||
                   lt == LUA_TNUMBER || lt == LUA_TBOOLEAN;
    }
}

// 按目标参数类型把 Lua 值装箱成 Java 对象，返回局部引用（调用方负责 DeleteLocalRef）
static jobject box_arg_for_class(JNIEnv* env, lua_State* L, int idx, jclass pcls) {
    char t = java_class_to_type_char(env, pcls);
    int lt = lua_type(L, idx);
    jclass boxCls; jmethodID valueOf;
    switch (t) {
        case 'I':
            boxCls = (*env)->FindClass(env, "java/lang/Integer");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(I)Ljava/lang/Integer;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jint)lua_tointeger(L, idx));
        case 'J':
            boxCls = (*env)->FindClass(env, "java/lang/Long");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(J)Ljava/lang/Long;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jlong)lua_tointeger(L, idx));
        case 'D':
            boxCls = (*env)->FindClass(env, "java/lang/Double");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(D)Ljava/lang/Double;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jdouble)lua_tonumber(L, idx));
        case 'F':
            boxCls = (*env)->FindClass(env, "java/lang/Float");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(F)Ljava/lang/Float;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jfloat)lua_tonumber(L, idx));
        case 'Z':
            boxCls = (*env)->FindClass(env, "java/lang/Boolean");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(Z)Ljava/lang/Boolean;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jboolean)lua_toboolean(L, idx));
        case 'B':
            boxCls = (*env)->FindClass(env, "java/lang/Byte");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(B)Ljava/lang/Byte;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jbyte)lua_tointeger(L, idx));
        case 't':
            boxCls = (*env)->FindClass(env, "java/lang/Short");
            valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(S)Ljava/lang/Short;");
            return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jshort)lua_tointeger(L, idx));
        case 'C': {
            jchar ch;
            if (lt == LUA_TSTRING) { size_t len; const char* s = luaL_checklstring(L, idx, &len); ch = (jchar)(unsigned char)(len ? s[0] : 0); }
            else ch = (jchar)lua_tointeger(L, idx);
            boxCls = (*env)->FindClass(env, "java/lang/Character");
            jmethodID ctor = (*env)->GetMethodID(env, boxCls, "<init>", "(C)V");
            return (*env)->NewObject(env, boxCls, ctor, ch);
        }
        case 'S': {
            size_t len; const char* s = luaL_checklstring(L, idx, &len);
            return (*env)->NewStringUTF(env, s);
        }
        default: { // 'L' 引用类型
            if (lt == LUA_TUSERDATA) {
                JavaUserdata* ud = (JavaUserdata*)luaL_testudata(L, idx, JAVAOBJECT_META);
                if (ud) return (*env)->NewLocalRef(env, ud->obj);
                JavaArray* arr = (JavaArray*)luaL_testudata(L, idx, JAVAARRAY_META);
                if (arr) return (*env)->NewLocalRef(env, arr->arrayObj);
                return NULL;
            }
            if (lt == LUA_TSTRING) {
                size_t len; const char* s = luaL_checklstring(L, idx, &len);
                return (*env)->NewStringUTF(env, s);
            }
            if (lt == LUA_TNUMBER) {
                if (lua_isinteger(L, idx)) {
                    lua_Integer n = lua_tointeger(L, idx);
                    if (n >= -2147483648LL && n <= 2147483647LL) {
                        boxCls = (*env)->FindClass(env, "java/lang/Integer");
                        valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(I)Ljava/lang/Integer;");
                        return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jint)n);
                    }
                    boxCls = (*env)->FindClass(env, "java/lang/Long");
                    valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(J)Ljava/lang/Long;");
                    return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jlong)n);
                }
                boxCls = (*env)->FindClass(env, "java/lang/Double");
                valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(D)Ljava/lang/Double;");
                return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jdouble)lua_tonumber(L, idx));
            }
            if (lt == LUA_TBOOLEAN) {
                boxCls = (*env)->FindClass(env, "java/lang/Boolean");
                valueOf = (*env)->GetStaticMethodID(env, boxCls, "valueOf", "(Z)Ljava/lang/Boolean;");
                return (*env)->CallStaticObjectMethod(env, boxCls, valueOf, (jboolean)lua_toboolean(L, idx));
            }
            return NULL;
        }
    }
}

// 把 Java 对象（含装箱对象）转换为 Lua 值压栈，返回压入个数
static int push_boxed_object(lua_State* L, JNIEnv* env, jobject val) {
    if (!val) { lua_pushnil(L); return 1; }
    jclass ocls = (*env)->GetObjectClass(env, val);
    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    if ((*env)->IsInstanceOf(env, val, strCls)) {
        const char* s = (*env)->GetStringUTFChars(env, (jstring)val, NULL);
        lua_pushstring(L, s);
        (*env)->ReleaseStringUTFChars(env, (jstring)val, s);
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Integer"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "intValue", "()I");
        lua_pushinteger(L, (*env)->CallIntMethod(env, val, m));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Long"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "longValue", "()J");
        lua_pushinteger(L, (lua_Integer)(*env)->CallLongMethod(env, val, m));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Double"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "doubleValue", "()D");
        lua_pushnumber(L, (*env)->CallDoubleMethod(env, val, m));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Float"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "floatValue", "()F");
        lua_pushnumber(L, (*env)->CallFloatMethod(env, val, m));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Boolean"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "booleanValue", "()Z");
        lua_pushboolean(L, (*env)->CallBooleanMethod(env, val, m));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Short"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "shortValue", "()S");
        char buf[16]; snprintf(buf, sizeof(buf), "%d", (int)(*env)->CallShortMethod(env, val, m));
        lua_pushinteger(L, atoll(buf));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Byte"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "byteValue", "()B");
        lua_pushinteger(L, (jbyte)(*env)->CallByteMethod(env, val, m));
    } else if ((*env)->IsInstanceOf(env, val, (*env)->FindClass(env, "java/lang/Character"))) {
        jmethodID m = (*env)->GetMethodID(env, ocls, "charValue", "()C");
        jchar ch = (*env)->CallCharMethod(env, val, m);
        lua_pushlstring(L, (const char*)&ch, 1);
    } else {
        new_java_object_ud(L, val);
    }
    (*env)->DeleteLocalRef(env, strCls);
    (*env)->DeleteLocalRef(env, ocls);
    return 1;
}

// 反射查找并调用方法（Method.invoke），支持任意返回类型与自动装箱。
// 逐个尝试匹配的重载；某个重载抛异常时继续尝试下一个。成功返回压入 Lua 栈的个数，
// 全部失败返回 -1。
static int invoke_reflective(lua_State* L, JNIEnv* env, jclass cls, jobject obj,
                             const char* name, int wantStatic,
                             int startIdx, int nargs) {
    jclass classCls = (*env)->FindClass(env, "java/lang/Class");
    jmethodID getMethods = (*env)->GetMethodID(env, classCls, "getMethods", "()[Ljava/lang/reflect/Method;");
    jobjectArray methods = (jobjectArray)(*env)->CallObjectMethod(env, cls, getMethods);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); (*env)->DeleteLocalRef(env, classCls); return -1; }
    jsize count = (*env)->GetArrayLength(env, methods);

    jclass methodCls = (*env)->FindClass(env, "java/lang/reflect/Method");
    jmethodID getName = (*env)->GetMethodID(env, methodCls, "getName", "()Ljava/lang/String;");
    jmethodID getParams = (*env)->GetMethodID(env, methodCls, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID getMods = (*env)->GetMethodID(env, methodCls, "getModifiers", "()I");
    jclass modCls = (*env)->FindClass(env, "java/lang/reflect/Modifier");
    jmethodID isStaticM = (*env)->GetStaticMethodID(env, modCls, "isStatic", "(I)Z");

    jclass objCls = (*env)->FindClass(env, "java/lang/Object");
    jclass fcl = (*env)->FindClass(env, "java/lang/reflect/Method");
    jmethodID invoke = (*env)->GetMethodID(env, fcl, "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");

    int pushed = -1;
    for (jsize i = 0; i < count; i++) {
        jobject m = (*env)->GetObjectArrayElement(env, methods, i);
        jstring mn = (jstring)(*env)->CallObjectMethod(env, m, getName);
        const char* mnc = (*env)->GetStringUTFChars(env, mn, NULL);
        int nameMatch = (strcmp(mnc, name) == 0);
        (*env)->ReleaseStringUTFChars(env, mn, mnc);
        (*env)->DeleteLocalRef(env, mn);
        if (!nameMatch) { (*env)->DeleteLocalRef(env, m); continue; }

        jint mods = (*env)->CallIntMethod(env, m, getMods);
        jboolean mStatic = (*env)->CallStaticBooleanMethod(env, modCls, isStaticM, mods);
        if ((jboolean)wantStatic != mStatic) { (*env)->DeleteLocalRef(env, m); continue; }

        jobjectArray ptypes = (jobjectArray)(*env)->CallObjectMethod(env, m, getParams);
        jsize pcount = (*env)->GetArrayLength(env, ptypes);
        if (pcount != (jsize)nargs) {
            (*env)->DeleteLocalRef(env, ptypes);
            (*env)->DeleteLocalRef(env, m);
            continue;
        }

        int compat = 1;
        jclass* pcs = (jclass*)calloc(nargs > 0 ? (size_t)nargs : 1, sizeof(jclass));
        for (jsize p = 0; p < pcount; p++) {
            jclass pc = (jclass)(*env)->GetObjectArrayElement(env, ptypes, p);
            pcs[p] = pc;
            if (!lua_compat_with_class(env, L, startIdx + (int)p, pc)) { compat = 0; break; }
        }
        (*env)->DeleteLocalRef(env, ptypes);
        if (!compat) {
            for (jsize p = 0; p < pcount; p++) (*env)->DeleteLocalRef(env, pcs[p]);
            free(pcs);
            (*env)->DeleteLocalRef(env, m);
            continue;
        }

        // 候选匹配：装箱参数并调用，抛异常则尝试下一个候选
        jobjectArray argsArr = (*env)->NewObjectArray(env, nargs, objCls, NULL);
        for (int k = 0; k < nargs; k++) {
            jobject boxed = box_arg_for_class(env, L, startIdx + k, pcs[k]);
            if (boxed) (*env)->SetObjectArrayElement(env, argsArr, k, boxed);
            (*env)->DeleteLocalRef(env, boxed);
        }
        for (jsize p = 0; p < pcount; p++) (*env)->DeleteLocalRef(env, pcs[p]);
        free(pcs);

        jobject result = (*env)->CallObjectMethod(env, m, invoke, wantStatic ? NULL : obj, argsArr);
        (*env)->DeleteLocalRef(env, argsArr);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteLocalRef(env, m);
            continue; // 尝试下一个重载
        }
        pushed = push_boxed_object(L, env, result);
        (*env)->DeleteLocalRef(env, result);
        (*env)->DeleteLocalRef(env, m);
        break;
    }

    (*env)->DeleteLocalRef(env, objCls);
    (*env)->DeleteLocalRef(env, fcl);
    (*env)->DeleteLocalRef(env, modCls);
    (*env)->DeleteLocalRef(env, methodCls);
    (*env)->DeleteLocalRef(env, classCls);
    (*env)->DeleteLocalRef(env, methods);
    return pushed;
}

// 判断类（含继承链上的 public 方法）是否存在名为 name 的方法。
// 用于 java_object_index：方法优先于字段，避免字段遮蔽方法（如 BigDecimal.scale）。
static int class_has_method(JNIEnv* env, jclass cls, const char* name) {
    jclass classCls = (*env)->FindClass(env, "java/lang/Class");
    if (!classCls) { (*env)->ExceptionClear(env); return 0; }
    jmethodID getMethods = (*env)->GetMethodID(env, classCls, "getMethods", "()[Ljava/lang/reflect/Method;");
    jobjectArray methods = (jobjectArray)(*env)->CallObjectMethod(env, cls, getMethods);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, classCls);
        return 0;
    }
    jsize count = (*env)->GetArrayLength(env, methods);
    jclass methodCls = (*env)->FindClass(env, "java/lang/reflect/Method");
    jmethodID getName = (*env)->GetMethodID(env, methodCls, "getName", "()Ljava/lang/String;");
    int found = 0;
    for (jsize i = 0; i < count; i++) {
        jobject m = (*env)->GetObjectArrayElement(env, methods, i);
        jstring mn = (jstring)(*env)->CallObjectMethod(env, m, getName);
        const char* mnc = (*env)->GetStringUTFChars(env, mn, NULL);
        int match = (strcmp(mnc, name) == 0);
        (*env)->ReleaseStringUTFChars(env, mn, mnc);
        (*env)->DeleteLocalRef(env, mn);
        (*env)->DeleteLocalRef(env, m);
        if (match) { found = 1; break; }
    }
    (*env)->DeleteLocalRef(env, methods);
    (*env)->DeleteLocalRef(env, methodCls);
    (*env)->DeleteLocalRef(env, classCls);
    return found;
}

// ========== 类型推导辅助 ==========
// ========== 恢复后的 get_possible_arg_types（原始逻辑 + static 修复） ==========
static void get_possible_arg_types(char c, char** options, int* count) {
    static char intOptions[4][32]    = {"I","D","J","Ljava/lang/Object;"};
    static char doubleOptions[2][32] = {"D","Ljava/lang/Object;"};
    static char stringOptions[2][32] = {"Ljava/lang/String;","Ljava/lang/Object;"};
    static char boolOptions[2][32]   = {"Z","Ljava/lang/Object;"};
    static char byteArrayOptions[1][32] = {"[B"};
    static char objOptions[1][32]    = {"Ljava/lang/Object;"};
    static char* optPtrs[4];  // static 确保指针生命周期

    switch (c) {
        case 'I':
            optPtrs[0] = intOptions[0];
            optPtrs[1] = intOptions[1];
            optPtrs[2] = intOptions[2];
            optPtrs[3] = intOptions[3];
            *options = (char*)optPtrs;
            *count = 4;
            break;
        case 'D':
        case 'F':
            optPtrs[0] = doubleOptions[0];
            optPtrs[1] = doubleOptions[1];
            *options = (char*)optPtrs;
            *count = 2;
            break;
        case 'S':
            optPtrs[0] = stringOptions[0];
            optPtrs[1] = stringOptions[1];
            *options = (char*)optPtrs;
            *count = 2;
            break;
        case 'Z':
            optPtrs[0] = boolOptions[0];
            optPtrs[1] = boolOptions[1];
            *options = (char*)optPtrs;
            *count = 2;
            break;
        case 'A':
            optPtrs[0] = objOptions[0];
            *options = (char*)optPtrs;
            *count = 1;
            break;
        default:
            optPtrs[0] = objOptions[0];
            *options = (char*)optPtrs;
            *count = 1;
            break;
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
            case 'S': strcat(sigBuf,"Ljava/lang/String;"); break;
            case 'I': strcat(sigBuf,"I"); break;
            case 'D': strcat(sigBuf,"D"); break;
            case 'F': strcat(sigBuf,"F"); break;
            case 'Z': strcat(sigBuf,"Z"); break;
            case 'V': strcat(sigBuf,"V"); break;
            case 'J': strcat(sigBuf,"J"); break;
            default:  strcat(sigBuf,"Ljava/lang/Object;"); break;
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
    char* options;
    int count;
    get_possible_arg_types(c, &options, &count);
    char** optPtrs = (char**)options;
    for (int i = 0; i < count; i++) {
        char saved[128];
        strcpy(saved, sigBuf);
        strcat(sigBuf, optPtrs[i]);
        jmethodID m = try_method_combinations(env, cls, name, L, startIdx, nargs, returnType, argIdx+1, sigBuf, outReturnType, isStatic);
        if (m) return m;
        strcpy(sigBuf, saved);
    }
    return NULL;
}

static jmethodID try_find_method(JNIEnv* env, jclass cls, const char* name,
                                  lua_State* L, int startIdx, int nargs,
                                  char* outReturnType, int isStatic) {
    char returnTypes[] = {'S','I','Z','D','F','J','V','O',0};
    for (char* rt=returnTypes; *rt; rt++) {
        char sig[128] = "(";
        jmethodID m = try_method_combinations(env, cls, name, L, startIdx, nargs, *rt, 0, sig, outReturnType, isStatic);
        if (m) return m;
    }
    return NULL;
}

static void push_jni_args(lua_State* L, JNIEnv* env, int startIdx, int nargs, jvalue* args, char* argTypes) {
    for (int i = 0; i < nargs; i++) {
        int idx = startIdx + i;
        switch (argTypes[i]) {
            case 'I': args[i].i = (jint)lua_tointeger(L, idx); break;
            case 'J': args[i].j = (jlong)lua_tointeger(L, idx); break;
            case 'D': args[i].d = (jdouble)lua_tonumber(L, idx); break;
            case 'F': args[i].f = (jfloat)lua_tonumber(L, idx); break;
            case 'Z': args[i].z = (jboolean)lua_toboolean(L, idx); break;
            case 'S': args[i].l = lua_check_jstring(env, L, idx); break;
            default: {
                if (lua_isuserdata(L, idx)) {
                    if (lua_getmetatable(L, idx)) {
                        luaL_getmetatable(L, JAVAOBJECT_META);
                        int isJavaObject = lua_rawequal(L, -1, -2);
                        lua_pop(L, 2);
                        if (isJavaObject) {
                            JavaUserdata* ud = (JavaUserdata*)lua_touserdata(L, idx);
                            if (ud) { args[i].l = ud->obj; break; }
                        }
                    }
                    if (lua_getmetatable(L, idx)) {
                        luaL_getmetatable(L, JAVAARRAY_META);
                        int isJavaArray = lua_rawequal(L, -1, -2);
                        lua_pop(L, 2);
                        if (isJavaArray) {
                            JavaArray* arr = (JavaArray*)lua_touserdata(L, idx);
                            if (arr) { args[i].l = arr->arrayObj; break; }
                        }
                    }
                }
                args[i].l = NULL;
                break;
            }
        }
    }
}

static int push_java_result(lua_State* L, JNIEnv* env, jvalue result, char returnType) {
    switch (returnType) {
        case 'V': lua_pushnil(L); return 1;
        case 'I': lua_pushinteger(L, (lua_Integer)result.i); return 1;
        case 'D': lua_pushnumber(L, (lua_Number)result.d); return 1;
        case 'F': lua_pushnumber(L, (lua_Number)result.f); return 1;
        case 'Z': lua_pushboolean(L, result.z); return 1;
        case 'J': lua_pushinteger(L, (lua_Integer)result.j); return 1;
        case 'S': {
            if (!result.l) { lua_pushnil(L); }
            else { const char* s = (*env)->GetStringUTFChars(env, (jstring)result.l, NULL);
                   lua_pushstring(L, s); (*env)->ReleaseStringUTFChars(env, (jstring)result.l, s); }
            return 1;
        }
        default:
            // Object 返回值：优先转换为 Lua 原生类型（String/数字/布尔/数组），
            // 其余对象包装为 Java 对象 userdata —— 与 Java→Lua 传参行为一致
            if (!result.l) { lua_pushnil(L); }
            else { push_java_arg(L, env, result.l); }
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

static int method_lookup_call(lua_State* L) {
    JNIEnv* env = getEnv();
    MethodLookup* ml = (MethodLookup*)luaL_checkudata(L, 1, METHODLOOKUP_META);
    if (!ml) {
        luaL_error(L, "invalid method lookup");
        return 0;
    }

    int firstArgIdx = 2;
    int nargs = lua_gettop(L) - 1;

    // 处理实例方法调用时的 self 参数（instance:method()）
    if (ml->isStatic == 0 && nargs >= 1 && lua_isuserdata(L, firstArgIdx)) {
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

    // 处理类方法调用时的 self 参数（Class:method() 冒号语法）。
    // 类方法查找（isStatic == -1 自动检测，或显式静态）同样需剥离冒号传入的类本身，
    // 否则类对象会被当成第一个实参参与签名匹配，导致 "method not found"。
    if ((ml->isStatic == -1 || ml->isStatic == 1) && nargs >= 1 && lua_isuserdata(L, firstArgIdx)) {
        if (lua_getmetatable(L, firstArgIdx)) {
            luaL_getmetatable(L, JAVACLASS_META);
            int isCls = lua_rawequal(L, -1, -2);
            lua_pop(L, 2);
            if (isCls) {
                JavaUserdata* ud = (JavaUserdata*)lua_touserdata(L, firstArgIdx);
                if (ud && (*env)->IsSameObject(env, ud->obj, ml->obj)) {
                    firstArgIdx = 3;
                    nargs = lua_gettop(L) - 2;
                }
            }
        }
    }

    jclass cls = NULL;
    int actualIsStatic = ml->isStatic;
    char returnType = 'O';
    jmethodID method = NULL;

    if (ml->isStatic == 1) {
        cls = (jclass)ml->obj;
        actualIsStatic = 1;
        method = try_find_method(env, cls, ml->methodName, L, firstArgIdx, nargs, &returnType, 1);
    } else if (ml->isStatic == 0) {
        cls = (*env)->GetObjectClass(env, ml->obj);
        actualIsStatic = 0;
        method = try_find_method(env, cls, ml->methodName, L, firstArgIdx, nargs, &returnType, 0);
    } else {
        // isStatic == -1: 自动检测（ml->obj 是 Class 对象）
        // 先尝试静态方法
        cls = (jclass)ml->obj;
        method = try_find_method(env, cls, ml->methodName, L, firstArgIdx, nargs, &returnType, 1);
        if (method) {
            actualIsStatic = 1;
        } else {
            // 再尝试实例方法（Class 对象本身作为实例）
            cls = (*env)->GetObjectClass(env, ml->obj);
            method = try_find_method(env, cls, ml->methodName, L, firstArgIdx, nargs, &returnType, 0);
            if (method) {
                actualIsStatic = 0;
            }
        }
    }

    // 关键修改：常规方法查找失败，尝试反射调用（支持任意返回类型，如 java.math 的方法）
    if (!method) {
        if (ml->isStatic == 1 || ml->isStatic == -1) {
            // isStatic==-1 时 cls 可能已被重赋值为 Class 对象的类，静态查找需用原类
            jclass scls = (ml->isStatic == 1) ? cls : (jclass)ml->obj;
            int pushed = invoke_reflective(L, env, scls, NULL, ml->methodName, 1, firstArgIdx, nargs);
            if (pushed != -1) {
                if (cls && ml->isStatic != 1) (*env)->DeleteLocalRef(env, cls);
                return pushed;
            }
        }
        if (ml->isStatic == 0 || ml->isStatic == -1) {
            int pushed = invoke_reflective(L, env, cls, ml->obj, ml->methodName, 0, firstArgIdx, nargs);
            if (pushed != -1) {
                if (cls && ml->isStatic != 1) (*env)->DeleteLocalRef(env, cls);
                return pushed;
            }
        }
        if (cls && ml->isStatic != 1) (*env)->DeleteLocalRef(env, cls);
        luaL_error(L, "method not found: %s", ml->methodName);
        return 0;
    }

    // 构建参数
    char argTypes[16];
    for (int i = 0; i < nargs && i < 16; i++) {
        argTypes[i] = get_java_type_char(L, firstArgIdx + i);
    }

    jvalue args[nargs];
    memset(args, 0, sizeof(args));
    push_jni_args(L, env, firstArgIdx, nargs, args, argTypes);

    jvalue result;
    memset(&result, 0, sizeof(result));

    if (actualIsStatic == 1) {
        switch (returnType) {
            case 'S': result.l = (*env)->CallStaticObjectMethodA(env, cls, method, args); break;
            case 'I': result.i = (*env)->CallStaticIntMethodA(env, cls, method, args); break;
            case 'D': result.d = (*env)->CallStaticDoubleMethodA(env, cls, method, args); break;
            case 'Z': result.z = (*env)->CallStaticBooleanMethodA(env, cls, method, args); break;
            case 'V': (*env)->CallStaticVoidMethodA(env, cls, method, args); break;
            case 'J': result.j = (*env)->CallStaticLongMethodA(env, cls, method, args); break;
            default:  result.l = (*env)->CallStaticObjectMethodA(env, cls, method, args); break;
        }
    } else {
        switch (returnType) {
            case 'S': result.l = (*env)->CallObjectMethodA(env, ml->obj, method, args); break;
            case 'I': result.i = (*env)->CallIntMethodA(env, ml->obj, method, args); break;
            case 'D': result.d = (*env)->CallDoubleMethodA(env, ml->obj, method, args); break;
            case 'Z': result.z = (*env)->CallBooleanMethodA(env, ml->obj, method, args); break;
            case 'V': (*env)->CallVoidMethodA(env, ml->obj, method, args); break;
            case 'J': result.j = (*env)->CallLongMethodA(env, ml->obj, method, args); break;
            default:  result.l = (*env)->CallObjectMethodA(env, ml->obj, method, args); break;
        }
    }

    // 清理字符串参数
    for (int i = 0; i < nargs && i < 16; i++) {
        if (argTypes[i] == 'S' && args[i].l) {
            (*env)->DeleteLocalRef(env, args[i].l);
        }
    }

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        if (cls && ml->isStatic != 1) (*env)->DeleteLocalRef(env, cls);
        luaL_error(L, "method threw exception: %s", ml->methodName);
        return 0;
    }

    if (cls && ml->isStatic != 1) (*env)->DeleteLocalRef(env, cls);
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
    if (strcmp(key, "new") == 0) {
        lua_pushcfunction(L, java_class_call);
        return 1;
    }
    jclass cls = (jclass)ud->obj;

    // 尝试读取静态字段 (String)
    jfieldID field = (*env)->GetStaticFieldID(env, cls, key, "Ljava/lang/String;");
    if (field && !(*env)->ExceptionCheck(env)) {
        jstring val = (jstring)(*env)->GetStaticObjectField(env, cls, field);
        if (val) {
            const char* c = (*env)->GetStringUTFChars(env, val, NULL);
            lua_pushstring(L, c);
            (*env)->ReleaseStringUTFChars(env, val, c);
            (*env)->DeleteLocalRef(env, val);
            return 1;
        }
        lua_pushnil(L);
        return 1;
    }
    (*env)->ExceptionClear(env);

    // 尝试读取静态字段 (int)
    field = (*env)->GetStaticFieldID(env, cls, key, "I");
    if (field && !(*env)->ExceptionCheck(env)) {
        lua_pushinteger(L, (*env)->GetStaticIntField(env, cls, field));
        return 1;
    }
    (*env)->ExceptionClear(env);

    // 尝试读取静态字段 (boolean)
    field = (*env)->GetStaticFieldID(env, cls, key, "Z");
    if (field && !(*env)->ExceptionCheck(env)) {
        lua_pushboolean(L, (*env)->GetStaticBooleanField(env, cls, field));
        return 1;
    }
    (*env)->ExceptionClear(env);

    // 尝试读取静态字段 (double)
    field = (*env)->GetStaticFieldID(env, cls, key, "D");
    if (field && !(*env)->ExceptionCheck(env)) {
        lua_pushnumber(L, (*env)->GetStaticDoubleField(env, cls, field));
        return 1;
    }
    (*env)->ExceptionClear(env);

    // 返回 MethodLookup，isStatic = -1 表示自动检测
    return new_method_lookup(L, ud->obj, key, -1);
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
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVAOBJECT_META);
    if (!ud || !ud->obj) {
        lua_pushstring(L, "Java.Object (released)");
        return 1;
    }
    JNIEnv* env = getEnv();
    jclass cls = (*env)->GetObjectClass(env, ud->obj);
    jmethodID toString = (*env)->GetMethodID(env, cls, "toString", "()Ljava/lang/String;");
    if (toString && !(*env)->ExceptionCheck(env)) {
        jstring js = (jstring)(*env)->CallObjectMethod(env, ud->obj, toString);
        if (js) {
            const char* s = (*env)->GetStringUTFChars(env, js, NULL);
            lua_pushstring(L, s);
            (*env)->ReleaseStringUTFChars(env, js, s);
            (*env)->DeleteLocalRef(env, js);
        } else {
            lua_pushstring(L, "Java.Object");
        }
    } else {
        (*env)->ExceptionClear(env);
        lua_pushstring(L, "Java.Object");
    }
    (*env)->DeleteLocalRef(env, cls);
    return 1;
}

static int java_object_index(lua_State* L) {
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, 1, JAVAOBJECT_META);
    const char* key = luaL_checkstring(L, 2);
    JNIEnv* env = getEnv();
    jclass cls = (*env)->GetObjectClass(env, ud->obj);

    // 方法优先：存在同名方法时返回方法查找，避免字段遮蔽方法（如 BigDecimal.scale()）
    if (class_has_method(env, cls, key)) {
        (*env)->DeleteLocalRef(env, cls);
        return new_method_lookup(L, ud->obj, key, 0);
    }

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
    lua_state_add_ref(L); // 该 handler 持有状态引用，防止状态被提前 lua_close

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


int promise_next_id = 1;
PromiseEntry* promise_registry = NULL;

// 查找条目（调用方须持有 promise_mutex）
PromiseEntry* promise_find(int id) {
    PromiseEntry* e = promise_registry;
    while (e) { if (e->id == id) return e; e = e->next; }
    return NULL;
}

// 从链表摘除并释放条目（调用方须持有 promise_mutex）
void promise_remove(PromiseEntry* target) {
    PromiseEntry** pp = &promise_registry;
    while (*pp) {
        if (*pp == target) {
            *pp = target->next;
            if (target->result) free(target->result);
            free(target);
            return;
        }
        pp = &(*pp)->next;
    }
}

static int java_promise(lua_State* L) {
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* entry = (PromiseEntry*)malloc(sizeof(PromiseEntry));
    entry->id = promise_next_id++;
    entry->co = NULL;
    entry->owner = L;
    entry->callbackRef = LUA_NOREF;
    entry->done = 0;
    entry->result = NULL;
    entry->next = promise_registry;
    promise_registry = entry;
    pthread_mutex_unlock(&promise_mutex);
    lua_pushinteger(L, entry->id);
    return 1;
}

static int java_await(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* entry = promise_find(id);
    if (!entry) {
        pthread_mutex_unlock(&promise_mutex);
        return luaL_error(L, "promise not found: %d", id);
    }
    entry->co = L;
    pthread_mutex_unlock(&promise_mutex);
    return lua_yield(L, 0);
}

// ========== 回调注册：java.onComplete(id, callback) ==========
// 回调签名：callback(err, result...)
//   err == nil    ：成功，result... 与 checkPromise 返回值语义一致
//   err == string ：失败，内容为 E: 后的错误信息
// 若任务已完成则立即派发（处理"先完成再注册回调"的竞态）。
static int java_onComplete(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* e = promise_find(id);
    if (!e) {
        pthread_mutex_unlock(&promise_mutex);
        return luaL_error(L, "promise not found: %d", id);
    }
    // 同一 promise 重复 then：释放旧回调引用（后者覆盖）
    if (e->callbackRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, e->callbackRef);
    e->owner = L;
    lua_pushvalue(L, 2);
    e->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

    int alreadyDone = e->done;
    lua_State* owner = e->owner;
    int cbRef = e->callbackRef;
    char* result = NULL;
    if (alreadyDone) {
        result = e->result ? strdup(e->result) : NULL;
        e->callbackRef = LUA_NOREF;
        promise_remove(e);
    }
    pthread_mutex_unlock(&promise_mutex);

    if (alreadyDone) {
        dispatch_callback(owner, cbRef, result);
        if (result) free(result);
    }
    return 0;
}

// ========== java.yield(ms)：让出 lua_mutex，等待回调派发 ==========
// 主线程在 _doFile/_doString 期间持有递归锁 lua_mutex，工作线程的
// dispatch_callback 拿不到锁，回调会一直阻塞到脚本结束。
// java.yield 短暂释放 lua_mutex 再重新获取，给工作线程执行回调的机会。
// 等待循环应使用 java.yield 替代 Thread.sleep（后者不释放 lua_mutex）。
static int java_yield(lua_State* L) {
    long ms = (long)luaL_optinteger(L, 1, 10);
    if (ms < 0) ms = 0;
    pthread_mutex_unlock(&lua_mutex);
    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) { req = rem; continue; }
        break;
    }
    pthread_mutex_lock(&lua_mutex);
    return 0;
}

static int java_agent_exec(lua_State* L) {
    int funcRef = (int)luaL_checkinteger(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
    lua_call(L, 0, 1);
    return 0;
}

static int java_complete(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    int nargs = lua_gettop(L) - 1;

    pthread_mutex_lock(&promise_mutex);
    PromiseEntry* entry = promise_find(id);
    if (!entry) {
        pthread_mutex_unlock(&promise_mutex);
        return luaL_error(L, "promise not found: %d", id);
    }
    if (entry->done) {
        pthread_mutex_unlock(&promise_mutex);
        return 0;
    }

    lua_State* co = entry->co;
    // 写结果（'S:' 前缀 + arg2 字符串，兼容 checkPromise 读取）
    if (nargs > 0) {
        size_t len;
        const char* s = lua_tolstring(L, 2, &len);
        if (s) {
            if (entry->result) free(entry->result);
            entry->result = malloc(len + 3);
            entry->result[0] = 'S';
            entry->result[1] = ':';
            memcpy(entry->result + 2, s, len);
            entry->result[len + 2] = '\0';
        }
    }
    entry->done = 1;

    int cbRef = entry->callbackRef;
    lua_State* owner = entry->owner;
    char* rcopy = NULL;
    if (cbRef != LUA_NOREF) {
        rcopy = entry->result ? strdup(entry->result) : NULL;
        entry->callbackRef = LUA_NOREF;
        promise_remove(entry);
    }
    pthread_mutex_unlock(&promise_mutex);

    // 恢复挂起的协程（不持 promise_mutex，避免协程内再调 java API 死锁）
    if (co) {
        for (int i = 2; i <= nargs + 1; i++) {
            lua_pushvalue(L, i);
            lua_xmove(L, co, 1);
        }
        int nres;
        if (lua_resume(co, L, nargs, &nres) != LUA_OK && nres != LUA_YIELD) {
            const char* msg = lua_tostring(co, -1);
            fprintf(stderr, "java.complete resume error: %s\n", msg ? msg : "unknown");
            lua_pop(co, 1);
        }
    }

    if (rcopy) {
        dispatch_callback(owner, cbRef, rcopy);
        free(rcopy);
    }
    return 0;
}


// ========== 跨语言全局存储 (哈希表优化版) ==========
#define INITIAL_BUCKETS 1024

typedef struct StoreEntry {
    char* key;
    int type;
    union {
        lua_Number numVal;
        lua_Integer intVal;
        char* strVal;
        int boolVal;
    } value;
    int isInteger;
    struct StoreEntry* next;
} StoreEntry;

static StoreEntry** store_hash = NULL;
static int bucket_count = INITIAL_BUCKETS;
static int total_entries = 0;

// FNV-1a 哈希算法 (快速且分布均匀)
static unsigned int hash_key(const char* key) {
    unsigned int hash = 2166136261u;
    while (*key) {
        hash ^= (unsigned char)(*key++);
        hash *= 16777619u;
    }
    return hash;
}

// 查找或创建条目 (减少重复代码)
static StoreEntry* find_or_create_entry(lua_State* L, const char* key, int create) {
    if (!store_hash) {
        store_hash = (StoreEntry**)calloc(bucket_count, sizeof(StoreEntry*));
    }
    
    unsigned int h = hash_key(key) % bucket_count;
    StoreEntry* e = store_hash[h];
    while (e) {
        if (strcmp(e->key, key) == 0) return e;
        e = e->next;
    }
    
    if (!create) return NULL;
    
    // 负载因子超过0.75时扩容
    if (total_entries > bucket_count * 0.75) {
        int old_count = bucket_count;
        bucket_count *= 2;
        StoreEntry** new_hash = (StoreEntry**)calloc(bucket_count, sizeof(StoreEntry*));
        
        for (int i = 0; i < old_count; i++) {
            StoreEntry* entry = store_hash[i];
            while (entry) {
                StoreEntry* next = entry->next;
                unsigned int new_h = hash_key(entry->key) % bucket_count;
                entry->next = new_hash[new_h];
                new_hash[new_h] = entry;
                entry = next;
            }
        }
        free(store_hash);
        store_hash = new_hash;
        h = hash_key(key) % bucket_count;
    }
    
    e = (StoreEntry*)malloc(sizeof(StoreEntry));
    e->key = strdup(key);
    e->value.strVal = NULL;
    e->type = LUA_TNIL;
    e->isInteger = 0;
    e->next = store_hash[h];
    store_hash[h] = e;
    total_entries++;
    return e;
}

static int java_store(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    StoreEntry* e = find_or_create_entry(L, key, 1);
    
    // 释放旧字符串 (仅当之前存储的是字符串)
    if (e->type == LUA_TSTRING && e->value.strVal) {
        free(e->value.strVal);
        e->value.strVal = NULL;
    }
    
    int t = lua_type(L, 2);
    e->type = t;
    
    switch (t) {
        case LUA_TNUMBER:
            if (lua_isinteger(L, 2)) {
                e->value.intVal = lua_tointeger(L, 2);
                e->isInteger = 1;
            } else {
                e->value.numVal = lua_tonumber(L, 2);
                e->isInteger = 0;
            }
            break;
        case LUA_TSTRING:
            e->value.strVal = strdup(lua_tostring(L, 2));
            break;
        case LUA_TBOOLEAN:
            e->value.boolVal = lua_toboolean(L, 2);
            break;
        default:
            e->type = LUA_TNIL;
            break;
    }
    return 0;
}

static int java_fetch(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    
    if (!store_hash) {
        lua_pushnil(L);
        return 1;
    }
    
    unsigned int h = hash_key(key) % bucket_count;
    StoreEntry* e = store_hash[h];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            switch (e->type) {
                case LUA_TNUMBER:
                    if (e->isInteger) lua_pushinteger(L, e->value.intVal);
                    else lua_pushnumber(L, e->value.numVal);
                    break;
                case LUA_TSTRING:
                    lua_pushstring(L, e->value.strVal);
                    break;
                case LUA_TBOOLEAN:
                    lua_pushboolean(L, e->value.boolVal);
                    break;
                default:
                    lua_pushnil(L);
                    break;
            }
            return 1;
        }
        e = e->next;
    }
    lua_pushnil(L);
    return 1;
}

static int java_deleteStore(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    
    if (!store_hash) return 0;
    
    unsigned int h = hash_key(key) % bucket_count;
    StoreEntry* prev = NULL;
    StoreEntry* e = store_hash[h];
    
    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else store_hash[h] = e->next;
            
            free(e->key);
            if (e->type == LUA_TSTRING && e->value.strVal) {
                free(e->value.strVal);
            }
            free(e);
            total_entries--;
            return 0;
        }
        prev = e;
        e = e->next;
    }
    return 0;
}

// 清理函数 (防止内存泄漏)
static int java_cleanup(lua_State* L) {
    if (!store_hash) return 0;
    
    for (int i = 0; i < bucket_count; i++) {
        StoreEntry* e = store_hash[i];
        while (e) {
            StoreEntry* next = e->next;
            free(e->key);
            if (e->type == LUA_TSTRING && e->value.strVal) {
                free(e->value.strVal);
            }
            free(e);
            e = next;
        }
    }
    free(store_hash);
    store_hash = NULL;
    total_entries = 0;
    return 0;
}
static const luaL_Reg javalib[] = {
    {"import",      java_import},
    {"toString",    java_toString},
    {"promise",     java_promise},
    {"await",       java_await},
    {"onComplete",  java_onComplete},
    {"yield",       java_yield},
    {"createProxy", java_createProxy},
    {"complete",    java_complete},
    {"newArray",    java_newArray},
    {"store",       java_store},
    {"fetch",       java_fetch},
    {"deleteStore", java_deleteStore},
    {"__agent_exec", java_agent_exec},
    {"runAsync",    java_runAsync},
    {"runAsyncObj", java_runAsyncObj},
    {"getObject",   java_getObject},
    {"checkPromise", java_checkPromise},
    {NULL, NULL}
};


int luaopen_java(lua_State* L) {
    create_metatables(L);
    luaL_newlib(L, javalib);
    return 1;
}
// 供 lualib_async.c 使用的辅助函数
jobject java_get_obj(lua_State* L, int idx) {
    JavaUserdata* ud = (JavaUserdata*)luaL_checkudata(L, idx, JAVAOBJECT_META);
    return ud ? ud->obj : NULL;
}
