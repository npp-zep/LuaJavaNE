#ifndef LUAJAVA_UTF8_UTILS_H
#define LUAJAVA_UTF8_UTILS_H

#include <jni.h>
#include <lua.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Java 与 Lua 的字符串编码桥接。
 *
 * JNI 的 GetStringUTFChars / NewStringUTF 使用 "modified UTF-8"（CESU-8）：
 *   - 增补平面字符（如 emoji）会被拆成两个 3 字节代理项；
 *   - NUL 会被编码成 0xC0 0x80。
 * Lua 的字符串是标准 UTF-8，因此不能直接互转。
 * 这里统一用 UTF-16（GetStringChars / NewString）作为中转：
 *   - Java -> Lua：UTF-16 编码为标准 UTF-8（代理对合并为 4 字节）；
 *   - Lua -> Java：标准 UTF-8 解码为 UTF-16（4 字节序列拆为代理对）。
 * 非法序列或孤立代理项统一替换为 U+FFFD，保证输出仍是合法 UTF-8。
 */

/* 将 Java 字符串转为 malloc 分配的 UTF-8 缓冲区（以 NUL 结尾），失败返回 NULL。 */
static inline char* java_string_to_utf8(JNIEnv* env, jstring js, size_t* out_len) {
    if (!js) return NULL;
    jsize n = (*env)->GetStringLength(env, js);
    const jchar* chars = (*env)->GetStringChars(env, js, NULL);
    if (!chars) return NULL;

    /* 每个 UTF-16 码元最多产生 3 字节，代理对两个码元产生 4 字节；4*n+1 足够。 */
    char* out = (char*)malloc((size_t)n * 4 + 1);
    if (!out) {
        (*env)->ReleaseStringChars(env, js, chars);
        return NULL;
    }

    size_t o = 0;
    for (jsize i = 0; i < n; i++) {
        unsigned int cp = (unsigned int)chars[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 < n) {
                unsigned int lo = (unsigned int)chars[i + 1];
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    i++;
                } else {
                    cp = 0xFFFD;
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }

        if (cp < 0x80) {
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = '\0';
    (*env)->ReleaseStringChars(env, js, chars);
    if (out_len) *out_len = o;
    return out;
}

/* 将标准 UTF-8 字节序列转换为 Java String（UTF-16）；非法序列替换为 U+FFFD。 */
static inline jstring utf8_to_jstring(JNIEnv* env, const char* s, size_t len) {
    if (!s) return NULL;
    jchar* out = (jchar*)malloc((len ? len : 1) * sizeof(jchar));
    if (!out) return NULL;

    size_t o = 0, i = 0;
    while (i < len) {
        unsigned int cp = 0;
        size_t used = 0;
        unsigned char c = (unsigned char)s[i];

        if (c < 0x80) {
            cp = c;
            used = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len &&
                   ((unsigned char)s[i + 1] & 0xC0) == 0x80) {
            cp = ((unsigned int)(c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            used = 2;
            if (cp < 0x80) cp = 0xFFFD; /* overlong */
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len &&
                   ((unsigned char)s[i + 1] & 0xC0) == 0x80 &&
                   ((unsigned char)s[i + 2] & 0xC0) == 0x80) {
            cp = ((unsigned int)(c & 0x0F) << 12) |
                 ((unsigned int)((unsigned char)s[i + 1] & 0x3F) << 6) |
                 ((unsigned char)s[i + 2] & 0x3F);
            used = 3;
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < len &&
                   ((unsigned char)s[i + 1] & 0xC0) == 0x80 &&
                   ((unsigned char)s[i + 2] & 0xC0) == 0x80 &&
                   ((unsigned char)s[i + 3] & 0xC0) == 0x80) {
            cp = ((unsigned int)(c & 0x07) << 18) |
                 ((unsigned int)((unsigned char)s[i + 1] & 0x3F) << 12) |
                 ((unsigned int)((unsigned char)s[i + 2] & 0x3F) << 6) |
                 ((unsigned char)s[i + 3] & 0x3F);
            used = 4;
            if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
        } else {
            cp = 0xFFFD;
            used = 1;
        }

        if (cp > 0xFFFF) {
            unsigned int v = cp - 0x10000u;
            out[o++] = (jchar)(0xD800 + (v >> 10));
            out[o++] = (jchar)(0xDC00 + (v & 0x3FF));
        } else {
            out[o++] = (jchar)cp;
        }
        i += used;
    }

    jstring js = (*env)->NewString(env, out, (jsize)o);
    free(out);
    return js;
}

/* Java String -> Lua UTF-8 字符串（保留内嵌 NUL）。 */
static inline void lua_push_java_string(lua_State* L, JNIEnv* env, jstring js) {
    size_t len = 0;
    char* s = java_string_to_utf8(env, js, &len);
    if (!s) {
        lua_pushnil(L);
        return;
    }
    lua_pushlstring(L, s, len);
    free(s);
}

/* Lua 字符串 -> Java String。 */
static inline jstring lua_to_java_string(JNIEnv* env, lua_State* L, int idx) {
    size_t len = 0;
    const char* s = lua_tolstring(L, idx, &len);
    return utf8_to_jstring(env, s, len);
}

/* Java char（UTF-16 码元）-> Lua UTF-8 字符串。 */
static inline void lua_push_java_char(lua_State* L, jchar ch) {
    unsigned int cp = (unsigned int)ch;
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;

    char buf[3];
    size_t n;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    }
    lua_pushlstring(L, buf, n);
}

/* Lua 字符串 -> Java char：解码首个码点；增补平面返回高代理项。 */
static inline jchar lua_to_java_char(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        if (len == 0) return 0;

        unsigned int cp = 0;
        unsigned char c = (unsigned char)s[0];
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && len >= 2 &&
                   ((unsigned char)s[1] & 0xC0) == 0x80) {
            cp = ((unsigned int)(c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
            if (cp < 0x80) cp = 0xFFFD;
        } else if ((c & 0xF0) == 0xE0 && len >= 3 &&
                   ((unsigned char)s[1] & 0xC0) == 0x80 &&
                   ((unsigned char)s[2] & 0xC0) == 0x80) {
            cp = ((unsigned int)(c & 0x0F) << 12) |
                 ((unsigned int)((unsigned char)s[1] & 0x3F) << 6) |
                 ((unsigned char)s[2] & 0x3F);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        } else if ((c & 0xF8) == 0xF0 && len >= 4 &&
                   ((unsigned char)s[1] & 0xC0) == 0x80 &&
                   ((unsigned char)s[2] & 0xC0) == 0x80 &&
                   ((unsigned char)s[3] & 0xC0) == 0x80) {
            cp = ((unsigned int)(c & 0x07) << 18) |
                 ((unsigned int)((unsigned char)s[1] & 0x3F) << 12) |
                 ((unsigned int)((unsigned char)s[2] & 0x3F) << 6) |
                 ((unsigned char)s[3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
        } else {
            cp = 0xFFFD;
        }

        if (cp > 0xFFFF) {
            return (jchar)(0xD800 + ((cp - 0x10000u) >> 10));
        }
        return (jchar)cp;
    }
    return (jchar)lua_tointeger(L, idx);
}

#endif /* LUAJAVA_UTF8_UTILS_H */
