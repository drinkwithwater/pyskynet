#define LUA_LIB
#include "skynet.h"
#include "skynet_modify/skynet_modify.h"

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
    const char *value = skynet_modify_getlenv(key, &sz);
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
		conflict = skynet_modify_setlenv(key, value, sz) != 0;
		break;
	 }
    case LUA_TLIGHTUSERDATA: {
	    const char *value = lua_touserdata(L, 2);
        int sz = luaL_checkinteger(L, 3);
        if(sz < 0) {
            return luaL_error(L, "setlenv but size < 0 %d", sz);
        } else {
            conflict = skynet_modify_setlenv(key, value, sz) != 0;
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
    const char *nextkey = skynet_modify_nextenv(key);
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
    const char*data = skynet_modify_getscript(index, &sz);
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
    int index = skynet_modify_refscript(data, sz);
    lua_pushinteger(L, index);
    return 1;
}

static int llogprint(lua_State *L) {
	struct skynet_context * context = lua_touserdata(L, lua_upvalueindex(1));
    long loglevel = luaL_checkinteger(L, 1);
    const char * filename = luaL_checkstring(L, 2);
    long lineno = luaL_checkinteger(L, 3);
    const int MSG_INDEX = 4;
	int n = lua_gettop(L);
	if (n <= MSG_INDEX) {
		lua_settop(L, MSG_INDEX);
		const char * s = luaL_tolstring(L, MSG_INDEX, NULL);
		skynet_error(context, "|%d|%s|%d|%s", loglevel, filename, lineno, s);
		return 0;
	}
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	int i;
	for (i=MSG_INDEX; i<=n; i++) {
		luaL_tolstring(L, i, NULL);
		luaL_addvalue(&b);
		if (i<n) {
			luaL_addchar(&b, ' ');
		}
	}
	luaL_pushresult(&b);
	skynet_error(context, "|%d|%s|%d|%s", loglevel, filename, lineno, lua_tostring(L, -1));
	return 0;
}

static const struct luaL_Reg l_methods[] = {
    { "setlenv", lsetlenv},
    { "getlenv", lgetlenv},
    { "nextenv", lnextenv},
    { "cacheload", skynet_modify_cacheload},
    { "getscript", lgetscript},
    { "refscript", lrefscript},
    { NULL,  NULL },
};

static const struct luaL_Reg l_ctx_methods[] = {
    { "logprint", llogprint},
    { NULL,  NULL },
};

LUAMOD_API int luaopen_pyskynet_modify(lua_State *L) {
    luaL_checkversion(L);

    luaL_newlib(L, l_methods);

    lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
    struct skynet_context *ctx = lua_touserdata(L, -1);
    if (ctx == NULL)
    {
        return luaL_error(L, "Init skynet context first");
    }

    luaL_setfuncs(L, l_ctx_methods, 1);
    return 1;
}
