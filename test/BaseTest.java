package com.luajava;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

public class BaseTest {
    protected LuaRuntime L;

    @BeforeEach
    void setUp() {
        L = new LuaRuntime();
        L.doString("java = require 'java'");
    }

    @AfterEach
    void tearDown() {
        if (L != null) {
            L.close();
        }
    }
}
