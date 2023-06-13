#define LUA_LIB
#include "skynet.h"
#include "skynet_modify/skynet_py.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <time.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int
lgetlenv(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    size_t sz;
    const char *value = skynet_py_getlenv(key, &sz);
    if(value != NULL) {
        lua_pushlstring(L, value, sz);
        return 1;
    } else {
        return luaL_error(L, "getlenv with empty value key='%s'", key);
    }
}

static int
lsetlenv(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    bool conflict = false;
    int t2 = lua_type(L,2);
    switch (t2) {
    case LUA_TSTRING: {
        size_t sz;
		const char *value = lua_tolstring(L, 2, &sz);
		conflict = skynet_py_setlenv(key, value, sz) != 0;
		break;
	 }
    case LUA_TLIGHTUSERDATA: {
	    const char *value = lua_touserdata(L, 2);
        int sz = luaL_checkinteger(L, 3);
        if(sz < 0) {
            return luaL_error(L, "setlenv but size < 0 %d", sz);
        } else {
            conflict = skynet_py_setlenv(key, value, sz) != 0;
        }
	    break;
	 }
    default:
	    return luaL_error(L, "setlenv invalid param %s", lua_typename(L,t2));
    }
    if(conflict) {
	    return luaL_error(L, "setlenv but key conflict key='%s'", key);
    }
    return 0;
}

static int
lnextenv(lua_State *L) {
    const char *key = NULL;
    if(lua_type(L,1) == LUA_TSTRING) {
	   key = lua_tostring(L, 1);
    }
    const char *nextkey = skynet_py_nextenv(key);
    if(nextkey == NULL) {
	   lua_pushnil(L);
    } else {
	   lua_pushstring(L, nextkey);
    }
    return 1;
}

static int lgetscript(lua_State *L) {
    int index = luaL_checkinteger(L, 1);
    size_t sz;
    const char*data = skynet_py_getscript(index, &sz);
    if(data == NULL) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, data, sz);
    }
    return 1;
}

static int lrefscript(lua_State *L) {
    size_t sz;
    const char*data = luaL_checklstring(L, 1, &sz);
    int index = skynet_py_refscript(data, sz);
    lua_pushinteger(L, index);
    return 1;
}

static const struct luaL_Reg l_methods[] = {
    { "setlenv", lsetlenv},
    { "getlenv", lgetlenv},
    { "nextenv", lnextenv},
    { "cacheload", pyskynet_modify_cacheload},
    { "getscript", lgetscript},
    { "refscript", lrefscript},
    { NULL,  NULL },
};

LUAMOD_API int luaopen_pyskynet_modify(lua_State *L) {
    luaL_checkversion(L);

    luaL_newlib(L, l_methods);

    return 1;
}

