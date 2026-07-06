// native/lualib/lua_custom_init.c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

extern int luaopen_java(lua_State* L);
extern int luaopen_clac(lua_State* L);
extern int luaopen_gc(lua_State* L);  // 新增

static const luaL_Reg custom_libs[] = {
    {"java", luaopen_java},
    {"clac", luaopen_clac},
    {"gc",   luaopen_gc},    // 新增 GC 库
    {NULL, NULL}
};

void lua_open_custom_libs(lua_State* L) {
    const luaL_Reg* lib;
    for (lib = custom_libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }

    lua_pushstring(L,
        "LuaJavaNE 2.2.1 ("
        "Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR "." LUA_VERSION_RELEASE
        ", PUC-Rio)");
    lua_setglobal(L, "_VERSION");
}