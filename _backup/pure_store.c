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
