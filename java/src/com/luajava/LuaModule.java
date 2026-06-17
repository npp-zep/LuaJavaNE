package com.luajava;

import java.lang.annotation.*;

@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.TYPE)
public @interface LuaModule {
    String value() default "";
}
