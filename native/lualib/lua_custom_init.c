// native/lualib/lua_custom_init.c — 自定义库注册（不修改 Lua 源码）
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

// 声明自定义库的打开函数
extern int luaopen_java(lua_State* L);
extern int luaopen_clac(lua_State* L);

// 自定义库列表
static const luaL_Reg custom_libs[] = {
    {"java", luaopen_java},
    {"clac", luaopen_clac},
    {NULL, NULL}
};

// 注册自定义库到 lua_State
void lua_open_custom_libs(lua_State* L) {
    const luaL_Reg* lib;
    for (lib = custom_libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
}
