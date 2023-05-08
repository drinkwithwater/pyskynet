

#include "lua.h"
#include "lauxlib.h"
#include "spinlock.h"

struct skynet_env {
	struct spinlock lock;
	lua_State *L;
};

// mCodeLoaded used in loadfile. loadfile can take TypeHintLua file and return cached function
static struct skynet_env mCodeLoaded;

void skynet_py_initcodecache(void) {
	SPIN_INIT(&mCodeLoaded);
	mCodeLoaded.L = luaL_newstate();
}

static const void *
load_proto(const char *key) {
  if (mCodeLoaded.L == NULL)
    return NULL;
  SPIN_LOCK(&mCodeLoaded)
    lua_State *L = mCodeLoaded.L;
    lua_pushstring(L, key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    const void * result = lua_touserdata(L, -1);
    lua_pop(L, 1);
  SPIN_UNLOCK(&mCodeLoaded)

  return result;
}

static const void *
save_proto(const char *key, const void * proto) {
  lua_State *L;
  const void * result = NULL;

  SPIN_LOCK(&mCodeLoaded)
    L = mCodeLoaded.L;
    lua_pushstring(L, key);
    lua_pushvalue(L, -1);
    lua_rawget(L, LUA_REGISTRYINDEX);
    result = lua_touserdata(L, -1); /* stack: key oldvalue */
    if (result == NULL) {
      lua_pop(L,1);
      lua_pushlightuserdata(L, (void *)proto);
      lua_rawset(L, LUA_REGISTRYINDEX);
    } else {
      lua_pop(L,2);
    }

  SPIN_UNLOCK(&mCodeLoaded)
  return result;
}

// cacheload(key:String, missGet:Fn():Ret(String), mode:OrNil(String), env:Any)
int pyskynet_modify_cacheload(lua_State *L) {
  // 1. if exist
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 3, NULL);
  int envidx = (!lua_isnone(L, 4) ? 4 : 0);
  const void *proto = load_proto(filename);
  if (!proto) {
    // 2. if not exist then call missGet to get code after thlua compile
		lua_pushvalue(L, 2);
    int errCall = lua_pcall(L,0,1,0);
		if(errCall != LUA_OK) {
      lua_pushnil(L);
      lua_pushvalue(L, -2);
      return 2;
		}
    size_t codelen = 0;
    const char *codestr = luaL_checklstring(L, -1, &codelen);
    // 3. then, just do something like luaL_loadfilex_ in skynet
    lua_State * eL = luaL_newstate();
    if (eL == NULL) {
      lua_pushnil(L);
      lua_pushliteral(L, "New state failed");
      return 2;
    }
    int err = luaL_loadbufferx (eL, codestr, codelen, filename, mode);
    if (err != LUA_OK) {
      size_t sz = 0;
      const char * msg = lua_tolstring(eL, -1, &sz);
      lua_pushnil(L);
      lua_pushlstring(L, msg, sz);
      lua_close(eL);
      return 2;
    }
    lua_sharefunction(eL, -1);
    proto = lua_topointer(eL, -1);
    const void * oldv = save_proto(filename, proto);
    if (oldv) {
      lua_close(eL);
      lua_clonefunction(L, oldv);
    } else {
      lua_clonefunction(L, proto);
		/* Never close it. notice: memory leak */
    }
  } else {
    lua_clonefunction(L, proto);
  }
  if (envidx != 0) {  /* 'env' parameter? */
    lua_pushvalue(L, envidx);  /* environment for loaded function */
    if (!lua_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
	  lua_pop(L, 1);  /* remove 'env' if not used by previous call */
  }
  return 1;
}

