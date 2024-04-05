
#include "foreign_seri/seri.h"
#include "foreign_seri/write_block.h"
#include "foreign_seri/read_block.h"

/*************
 * unpack apis *
 *************/

inline void invalid_stream_line(lua_State *L, struct read_block *rb, int line) {
	int len = rb->len;
	luaL_error(L, "Invalid serialize stream %d (line:%d)", len, line);
}

#define invalid_stream(L,rb) invalid_stream_line(L,rb,__LINE__)

static uint8_t* lrb_unpack_one(lua_State *L, struct read_block *rb, bool in_table);

static void lrb_unpack_table(lua_State *L, struct read_block *rb, lua_Integer array_size) {
	if (array_size == MAX_COOKIE-1) {
		uint8_t *t = (uint8_t *)rb_read(rb, 1);
		if (t==NULL) {
			invalid_stream(L,rb);
		}
		uint8_t type = *t;
		int cookie = type >> 3;
		if ((type & 7) != TYPE_NUMBER || cookie == TYPE_NUMBER_REAL) {
			invalid_stream(L,rb);
		}
		if(!rb_get_integer(rb, cookie, &array_size)) {
			invalid_stream(L, rb);
		}
	}
	luaL_checkstack(L,LUA_MINSTACK,NULL);
	lua_createtable(L,array_size,0);
	int i;
	for (i=1;i<=array_size;i++) {
		lrb_unpack_one(L,rb, true);
		lua_rawseti(L,-2,i);
	}
	for (;;) {
		lrb_unpack_one(L,rb, true);
		if (lua_isnil(L,-1)) {
			lua_pop(L,1);
			return;
		}
		lrb_unpack_one(L,rb, true);
		lua_rawset(L,-3);
	}
}

static uint8_t* lrb_unpack_one(lua_State *L, struct read_block *rb, bool in_table) {
	uint8_t *aheadptr = (uint8_t*)rb_read(rb, 1);
	if (aheadptr==NULL) {
		if(in_table) {
			invalid_stream(L, rb);
		}
		return NULL;
	}
	uint8_t ahead = *aheadptr;
	int type = ahead & 0x7;
	int cookie = ahead >> 3;
	switch(type) {
        case TYPE_FOREIGN_USERDATA: {
            struct numsky_ndarray *arr = rb_get_nsarr(rb, cookie);
			if(arr==NULL) {
				invalid_stream(L, rb);
			} else {
				*(struct numsky_ndarray**)(lua_newuserdata(L, sizeof(struct numsky_ndarray*))) = arr;
				luaL_getmetatable(L, NS_ARR_METANAME);
				if(lua_isnil(L, -1)) {
					luaL_error(L, "require 'numsky' before use foreign seri");
				}
				lua_setmetatable(L, -2);
			}
            break;
        }
		case TYPE_NIL:
			lua_pushnil(L);
			break;
		case TYPE_BOOLEAN:
			lua_pushboolean(L,cookie);
			break;
		case TYPE_NUMBER:
			if (cookie == TYPE_NUMBER_REAL) {
				double value;
				if(rb_get_real(rb, &value)) {
					lua_pushnumber(L, value);
				} else {
					invalid_stream(L, rb);
				}
			} else {
				lua_Integer value;
				if(rb_get_integer(rb, cookie, &value)) {
					lua_pushinteger(L, value);
				} else {
					invalid_stream(L, rb);
				}
			}
			break;
		case TYPE_USERDATA: {
			void *value;
			if(rb_get_pointer(rb, &value)) {
				lua_pushlightuserdata(L, value);
			} else {
				invalid_stream(L, rb);
			}
			break;
		}
		case TYPE_SHORT_STRING:
		case TYPE_LONG_STRING: {
			size_t sz;
			char *p = rb_get_string(rb, ahead, &sz);
			if(p!=NULL) {
				lua_pushlstring(L, p, sz);
			} else {
				invalid_stream(L, rb);
			}
			break;
		}
		case TYPE_TABLE: {
			lrb_unpack_table(L,rb,cookie);
			break;
		}
		default: {
			invalid_stream(L,rb);
			break;
		}
    }
	return aheadptr;
}

inline static void foreign_wb_uint(struct write_block* wb, npy_intp v) {
	static const int B = 128;
	uint8_t data = v | B;
	while (v >= B) {
		data = v | B;
		wb_write(wb, &data, 1);
		v >>= 7;
	}
	data = (uint8_t)v;
	wb_write(wb, &data, 1);
}

/*************
 * pack apis *
 *************/

static void wb_put_nsarr(struct write_block *wb, struct numsky_ndarray* arr_obj) {
	// 1. nd & type
	uint8_t n = COMBINE_TYPE(TYPE_FOREIGN_USERDATA, arr_obj->nd);
	wb_write(wb, &n, 1);
	struct numsky_dtype *dtype = arr_obj->dtype;
	// 2. typechar
	wb_write(wb, &(dtype->typechar), 1);
	// 3. dimension
	for(int i=0;i<arr_obj->nd;i++) {
		foreign_wb_uint(wb, arr_obj->dimensions[i]);
	}
	if (wb->mode == MODE_FOREIGN) {
		// 4. strides
		wb_write(wb, arr_obj->strides, sizeof(npy_intp)*arr_obj->nd);
		// 5. data
		skynet_foreign_incref(arr_obj->foreign_base);
		wb_write(wb, &(arr_obj->foreign_base), sizeof(arr_obj->foreign_base));
		wb_write(wb, &(arr_obj->dataptr), sizeof(arr_obj->dataptr));
	} else if(wb->mode == MODE_FOREIGN_REMOTE){
		// 4. data
		struct numsky_nditer * iter = numsky_nditer_create(arr_obj);
		for(int i=0;i<iter->ao->count;numsky_nditer_next(iter), i++) {
			wb_write(wb, iter->dataptr, dtype->elsize);
		}
		numsky_nditer_destroy(iter);
	}
}

static void lwb_pack_one(lua_State *L, struct write_block *b, int index, int depth);

static int lwb_table_array(lua_State *L, struct write_block * wb, int index, int depth) {
	int array_size = lua_rawlen(L,index);
	if (array_size >= MAX_COOKIE-1) {
		uint8_t n = COMBINE_TYPE(TYPE_TABLE, MAX_COOKIE-1);
		wb_write(wb, &n, 1);
		wb_put_integer(wb, array_size);
	} else {
		uint8_t n = COMBINE_TYPE(TYPE_TABLE, array_size);
		wb_write(wb, &n, 1);
	}

	int i;
	for (i=1;i<=array_size;i++) {
		lua_rawgeti(L,index,i);
		lwb_pack_one(L, wb, -1, depth);
		lua_pop(L,1);
	}

	return array_size;
}

static void lwb_table_hash(lua_State *L, struct write_block * wb, int index, int depth, int array_size) {
	lua_pushnil(L);
	while (lua_next(L, index) != 0) {
		if (lua_type(L,-2) == LUA_TNUMBER) {
			if (lua_isinteger(L, -2)) {
				lua_Integer x = lua_tointeger(L,-2);
				if (x>0 && x<=array_size) {
					lua_pop(L,1);
					continue;
				}
			}
		}
		lwb_pack_one(L,wb,-2,depth);
		lwb_pack_one(L,wb,-1,depth);
		lua_pop(L, 1);
	}
	wb_nil(wb);
}

static void lwb_table_metapairs(lua_State *L, struct write_block *wb, int index, int depth) {
	uint8_t n = COMBINE_TYPE(TYPE_TABLE, 0);
	wb_write(wb, &n, 1);
	lua_pushvalue(L, index);
	lua_call(L, 1, 3);
	for(;;) {
		lua_pushvalue(L, -2);
		lua_pushvalue(L, -2);
		lua_copy(L, -5, -3);
		lua_call(L, 2, 2);
		int type = lua_type(L, -2);
		if (type == LUA_TNIL) {
			lua_pop(L, 4);
			break;
		}
		lwb_pack_one(L, wb, -2, depth);
		lwb_pack_one(L, wb, -1, depth);
		lua_pop(L, 1);
	}
	wb_nil(wb);
}

static void lwb_table(lua_State *L, struct write_block *wb, int index, int depth) {
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	if (index < 0) {
		index = lua_gettop(L) + index + 1;
	}
	if (luaL_getmetafield(L, index, "__pairs") != LUA_TNIL) {
		lwb_table_metapairs(L, wb, index, depth);
	} else {
		int array_size = lwb_table_array(L, wb, index, depth);
		lwb_table_hash(L, wb, index, depth, array_size);
	}
}

static void lwb_pack_one(lua_State *L, struct write_block *wb, int index, int depth) {
	if (depth > MAX_DEPTH) {
		wb_free(wb);
		luaL_error(L, "serialize can't pack too depth table");
	}
	int type = lua_type(L,index);
	switch(type) {
	case LUA_TNIL:
		wb_nil(wb);
		break;
	case LUA_TNUMBER: {
		if (lua_isinteger(L, index)) {
			lua_Integer x = lua_tointeger(L,index);
			wb_put_integer(wb, x);
		} else {
			lua_Number n = lua_tonumber(L,index);
			wb_put_real(wb,n);
		}
		break;
	}
	case LUA_TBOOLEAN:
		wb_boolean(wb, lua_toboolean(L,index));
		break;
	case LUA_TSTRING: {
		size_t sz = 0;
		const char *str = lua_tolstring(L,index,&sz);
		wb_put_string(wb, str, (int)sz);
		break;
	}
	case LUA_TLIGHTUSERDATA:
		wb_put_pointer(wb, lua_touserdata(L,index));
		break;
	case LUA_TTABLE: {
		if (index < 0) {
			index = lua_gettop(L) + index + 1;
		}
		lwb_table(L, wb, index, depth+1);
		break;
	}
	case LUA_TUSERDATA: {
		struct numsky_ndarray* arr = *(struct numsky_ndarray**) (luaL_checkudata(L, index, NS_ARR_METANAME));
		if(arr->nd >= MAX_COOKIE) {
			wb_free(wb);
			luaL_error(L, "numsky.ndarray's nd must be <= 31");
		}
		if (wb->mode==MODE_FOREIGN) {
			if(arr->foreign_base == NULL) {
				wb_free(wb);
				luaL_error(L, "foreign -base can't be null");
				return ;
			}
			wb_put_nsarr(wb, arr);
		} else if (wb->mode == MODE_FOREIGN_REMOTE) {
			wb_put_nsarr(wb, arr);
		} else {
			wb_free(wb);
			luaL_error(L, "[ERROR]wb_nsarr exception");
		}
		break;
	}
	default:
		wb_free(wb);
		luaL_error(L, "Unsupport type %s to serialize", lua_typename(L, type));
	}
}

static int lmode_pack(lua_State *L, int mode) {
	struct write_block wb;
	wb_init(&wb, mode);
	int n = lua_gettop(L);
	for(int i=1;i<=n;i++) {
		lwb_pack_one(L, &wb , i, 0);
	}
	lua_pushlightuserdata(L, wb.buffer);
	lua_pushinteger(L, wb.len);
    return 2;
}

static int lmode_unpack(lua_State *L, int mode){
	if (lua_isnoneornil(L,1)) {
		return 0;
	}
	void * buffer;
	int len;
	if (lua_type(L,1) == LUA_TSTRING) {
		size_t sz;
		 buffer = (void *)lua_tolstring(L,1,&sz);
		len = (int)sz;
	} else {
		buffer = lua_touserdata(L,1);
		len = luaL_checkinteger(L,2);
	}
	if (len == 0) {
		return 0;
	}
	if (buffer == NULL) {
		return luaL_error(L, "deserialize null pointer");
	}

	lua_settop(L,1);
	struct read_block rb;
	rb_init(&rb, (char*)buffer, len, mode);

	for (int i=0;;i++) {
		if (i%8==7) {
			luaL_checkstack(L,LUA_MINSTACK,NULL);
		}
		if(lrb_unpack_one(L, &rb, false) == NULL) {
			break;
		}
	}

	// Need not free buffer

	return lua_gettop(L) - 1;
}

static int lluapack(lua_State *L) {
	return lmode_pack(L, MODE_LUA);
}

static int lluaunpack(lua_State *L) {
	return lmode_unpack(L, MODE_LUA);
}

static int lpack(lua_State *L) {
	return lmode_pack(L, MODE_FOREIGN);
}

static int lunpack(lua_State *L) {
	return lmode_unpack(L, MODE_FOREIGN);
}

static int lremotepack(lua_State *L) {
	return lmode_pack(L, MODE_FOREIGN_REMOTE);
}

static int lremoteunpack(lua_State *L) {
	return lmode_unpack(L, MODE_FOREIGN_REMOTE);
}

static int ltostring(lua_State *L) {
	int t = lua_type(L,1);
	switch (t) {
	case LUA_TSTRING: {
		lua_settop(L, 1);
		return 1;
	}
	case LUA_TLIGHTUSERDATA: {
		char * msg = (char*)lua_touserdata(L,1);
		int sz = luaL_checkinteger(L,2);
		lua_pushlstring(L,msg,sz);
		return 1;
	}
	default:
		return 0;
	}
}

static int ltrash(lua_State *L) {
	int t = lua_type(L,1);
	switch (t) {
	case LUA_TSTRING: {
		break;
	}
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,1);
		luaL_checkinteger(L,2);
		skynet_free(msg);
		break;
	}
	default:
		luaL_error(L, "skynet.trash invalid param %s", lua_typename(L,t));
	}

	return 0;
}

static const struct luaL_Reg l_methods[] = {
    { "luapack" , lluapack },
    { "luaunpack", lluaunpack },
    { "pack", lpack },
    { "unpack" , lunpack },
    { "remotepack", lremotepack },
    { "remoteunpack", lremoteunpack },
    { "tostring", ltostring },
    { "trash", ltrash },
    { NULL,  NULL },
};

LUA_API int
luaopen_pyskynet_foreign_seri(lua_State *L) {
	luaL_checkversion(L);

	luaL_newlib(L, l_methods);
    return 1;
}
