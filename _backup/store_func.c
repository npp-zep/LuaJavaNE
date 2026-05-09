static int java_store(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    StoreEntry* e = store_registry;
    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (e->strVal) { free(e->strVal); e->strVal = NULL; }
            int t = lua_type(L, 2);
            e->type = t;
            switch (t) {
                case LUA_TNUMBER:
                    if (lua_isinteger(L, 2)) {
                        e->intVal = lua_tointeger(L, 2);
                        e->isInteger = 1;
                    } else {
                        e->numVal = lua_tonumber(L, 2);
                        e->isInteger = 0;
                    }
                    break;
                case LUA_TSTRING: e->strVal = strdup(lua_tostring(L, 2)); break;
                case LUA_TBOOLEAN: e->boolVal = lua_toboolean(L, 2); break;
                default: e->type = LUA_TNIL; break;
            }
            return 0;
        }
        e = e->next;
    }
    e = (StoreEntry*)malloc(sizeof(StoreEntry));
    e->key = strdup(key);
    e->strVal = NULL;
    int t = lua_type(L, 2);
    e->type = t;
    switch (t) {
        case LUA_TNUMBER:
            if (lua_isinteger(L, 2)) {
                e->intVal = lua_tointeger(L, 2);
                e->isInteger = 1;
            } else {
                e->numVal = lua_tonumber(L, 2);
                e->isInteger = 0;
            }
            break;
        case LUA_TSTRING: e->strVal = strdup(lua_tostring(L, 2)); break;
        case LUA_TBOOLEAN: e->boolVal = lua_toboolean(L, 2); break;
        default: e->type = LUA_TNIL; break;
    }
    e->next = store_registry;
    store_registry = e;
    return 0;
}

static int java_fetch(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    StoreEntry* e = store_registry;
    while (e) {
        if (strcmp(e->key, key) == 0) {
            switch (e->type) {
                case LUA_TNUMBER:
                    if (e->isInteger) lua_pushinteger(L, e->intVal);
                    else lua_pushnumber(L, e->numVal);
                    break;
                case LUA_TSTRING: lua_pushstring(L, e->strVal); break;
                case LUA_TBOOLEAN: lua_pushboolean(L, e->boolVal); break;
                default: lua_pushnil(L); break;
            }
            return 1;
        }
        e = e->next;
    }
    lua_pushnil(L); return 1;
}

static int java_deleteStore(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    StoreEntry* prev = NULL;
    StoreEntry* e = store_registry;
    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else store_registry = e->next;
            free(e->key);
            if (e->strVal) free(e->strVal);
            free(e);
            break;
        }
        prev = e;
        e = e->next;
    }
    return 0;
}
static const luaL_Reg javalib[] = {
    {"import",      java_import},
    {"toString",    java_toString},
    {"promise",     java_promise},
    {"await",       java_await},
    {"createProxy", java_createProxy},
    {"complete",    java_complete},
    {"newArray",    java_newArray},
    {"store",       java_store},
    {"fetch",       java_fetch},
    {"deleteStore", java_deleteStore},
    {NULL, NULL}
