#ifndef JNI_COMPAT_H
#define JNI_COMPAT_H

#include <jni.h>

/*
    这个头文件主要用于解决GCC与clang对于AttachCurrentThread参数类型的兼容性问题
    导入头文件后可以使用JNI_ATTACH(jvm, env)来代替AttachCurrentThread(jvm, &env, NULL)
    还能少写一个NULL
*/
#if defined(__clang__)
    // Clang: 使用 JNIEnv**
    #define JNI_ATTACH(jvm, env) (*jvm)->AttachCurrentThread(jvm, &env, NULL)
#elif defined(__GNUC__) || defined(__GNUG__)
    // GCC: 使用 void**
    #define JNI_ATTACH(jvm, env) (*jvm)->AttachCurrentThread(jvm, (void**)&env, NULL)
#else
    // 其他编译器：使用中间变量
    #define JNI_ATTACH(jvm, env) \
        do { \
            void *_tmp = NULL; \
            (*jvm)->AttachCurrentThread(jvm, &_tmp, NULL); \
            env = (JNIEnv*)_tmp; \
        } while(0)
#endif

#endif
