
#include "lua.h"
#include "lauxlib.h"
#include "spinlock.h"

struct skynet_env {
	struct spinlock lock;
	lua_State *L;
};

// mCodeScript used for scriptservice
static struct skynet_env mCodeScript;

void skynet_py_initscriptpool(void) {
	SPIN_INIT(&mCodeScript);
	mCodeScript.L = luaL_newstate();
}

// scriptservice_get
const char *skynet_py_getscript(int index, size_t *sz) {
  const char * data;
  SPIN_LOCK(&mCodeScript) {
    lua_pushglobaltable(mCodeScript.L);
    const int GLOBAL_TABLE = 1;
    int t = lua_rawgeti(mCodeScript.L, GLOBAL_TABLE, index);
    if(t==LUA_TSTRING) {
      data=lua_tolstring(mCodeScript.L, -1, sz);
    } else {
      data=NULL;
      sz = 0;
    }
    lua_settop(mCodeScript.L, 0);
  }
  SPIN_UNLOCK(&mCodeScript)
	return data;
}

int skynet_py_refscript(const char* data, size_t sz) {
  int index = 0;
  SPIN_LOCK(&mCodeScript) {
    lua_pushglobaltable(mCodeScript.L);
    const int GLOBAL_TABLE = 1;
    lua_pushlstring(mCodeScript.L, data, sz);
    lua_pushvalue(mCodeScript.L, -1);
    int t = lua_rawget(mCodeScript.L, GLOBAL_TABLE);
    if(t == LUA_TNUMBER && lua_isinteger(mCodeScript.L, -1)) {
      index = lua_tointeger(mCodeScript.L, -1);
    } else {
      lua_pop(mCodeScript.L, 1);
      lua_pushvalue(mCodeScript.L, -1);
      index = luaL_ref(mCodeScript.L, GLOBAL_TABLE);
      lua_pushinteger(mCodeScript.L, index);
      lua_rawset(mCodeScript.L, GLOBAL_TABLE);
    }
    lua_settop(mCodeScript.L, 0);
  }
  SPIN_UNLOCK(&mCodeScript)
  return index;
}