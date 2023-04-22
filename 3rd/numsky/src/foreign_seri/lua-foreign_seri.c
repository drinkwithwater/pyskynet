
#include "foreign_seri/seri.h"
#include "foreign_seri/write_block.h"
#include "foreign_seri/read_block.h"

inline static struct write_block* wb_cast(struct foreign_write_block * wb) {
    return (struct write_block*) wb;
}

inline static void foreign_wb_init(struct foreign_write_block *wb , struct block *b, int mode) {
    wb_init(wb_cast(wb), b);
    wb->mode = mode;
}


inline static void foreign_wb_uint(struct foreign_write_block* wb, npy_intp v) {
	static const int B = 128;
	uint8_t data = v | B;
	while (v >= B) {
		data = v | B;
		wb_push(wb_cast(wb), &data, 1);
		v >>= 7;
	}
	data = (uint8_t)v;
	wb_push(wb_cast(wb), &data, 1);
}


/*************
 * pack apis *
 *************/

/*used by foreign serialize, pass as a function pointer*/
static void foreign_wb_write(struct foreign_write_block *b, const void *buf, int sz) {
    wb_push(wb_cast(b), buf, sz);
}

static inline void wb_foreign(struct foreign_write_block *wb, struct numsky_ndarray* arr_obj) {
	// 1. nd & type
	uint8_t n = COMBINE_TYPE(TYPE_FOREIGN_USERDATA, arr_obj->nd);
	foreign_wb_write(wb, &n, 1);
	struct numsky_dtype *dtype = arr_obj->dtype;
	// 2. typechar
	foreign_wb_write(wb, &(dtype->typechar), 1);
	// 3. dimension
	for(int i=0;i<arr_obj->nd;i++) {
		foreign_wb_uint(wb, arr_obj->dimensions[i]);
	}
	if (wb->mode == MODE_FOREIGN) {
		// 4. strides
		foreign_wb_write(wb, arr_obj->strides, sizeof(npy_intp)*arr_obj->nd);
		// 5. data
		skynet_foreign_incref(arr_obj->foreign_base);
		foreign_wb_write(wb, &(arr_obj->foreign_base), sizeof(arr_obj->foreign_base));
		foreign_wb_write(wb, &(arr_obj->dataptr), sizeof(arr_obj->dataptr));
	} else if(wb->mode == MODE_FOREIGN_REMOTE){
		// 4. data
		struct numsky_nditer * iter = numsky_nditer_create(arr_obj);
		for(int i=0;i<iter->ao->count;numsky_nditer_next(iter), i++) {
			foreign_wb_write(wb, iter->dataptr, dtype->elsize);
		}
		numsky_nditer_destroy(iter);
	}
}


/* override pack_one */
static void foreign_pack_one(lua_State *L, struct foreign_write_block *b, int index, int depth);

/* override wb_table_array */
static int foreign_wb_table_array(lua_State *L, struct foreign_write_block * wb, int index, int depth) {
	int array_size = lua_rawlen(L,index);
	if (array_size >= MAX_COOKIE-1) {
		uint8_t n = COMBINE_TYPE(TYPE_TABLE, MAX_COOKIE-1);
		foreign_wb_write(wb, &n, 1);
		wb_integer(wb_cast(wb), array_size);
	} else {
		uint8_t n = COMBINE_TYPE(TYPE_TABLE, array_size);
		foreign_wb_write(wb, &n, 1);
	}

	int i;
	for (i=1;i<=array_size;i++) {
		lua_rawgeti(L,index,i);
		foreign_pack_one(L, wb, -1, depth);
		lua_pop(L,1);
	}

	return array_size;
}

/* override wb_table_hash */
static void foreign_wb_table_hash(lua_State *L, struct foreign_write_block * wb, int index, int depth, int array_size) {
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
		foreign_pack_one(L,wb,-2,depth);
		foreign_pack_one(L,wb,-1,depth);
		lua_pop(L, 1);
	}
	wb_nil(wb_cast(wb));
}

/* override wb_table */
static void foreign_wb_table(lua_State *L, struct foreign_write_block *wb, int index, int depth) {
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	if (index < 0) {
		index = lua_gettop(L) + index + 1;
	}
    int array_size = foreign_wb_table_array(L, wb, index, depth);
    foreign_wb_table_hash(L, wb, index, depth, array_size);
}

/* override pack_one */
static void foreign_pack_one(lua_State *L, struct foreign_write_block *wb, int index, int depth) {
	if (depth > MAX_DEPTH) {
		wb_free(wb_cast(wb));
		luaL_error(L, "serialize can't pack too depth table");
        return ;
	}
	int type = lua_type(L,index);
    switch(type) {
        case LUA_TUSERDATA: {
            struct numsky_ndarray* arr = *(struct numsky_ndarray**) (luaL_checkudata(L, index, NS_ARR_METANAME));
			if(arr->nd >= MAX_COOKIE) {
				luaL_error(L, "numsky.ndarray's nd must be <= 31");
			}
			if (wb->mode==MODE_FOREIGN) {
				if(arr->foreign_base == NULL) {
					luaL_error(L, "foreign -base can't be null");
					return ;
				}
				wb_foreign(wb, arr);
			} else if (wb->mode == MODE_FOREIGN_REMOTE) {
				wb_foreign(wb, arr);
			} else {
				luaL_error(L, "[ERROR]wb_foreign exception");
			}
            break;
        }
        case LUA_TTABLE: {
            if (index < 0) {
                index = lua_gettop(L) + index + 1;
            }
            foreign_wb_table(L, wb, index, depth+1);
            break;
        }
        default: {
            pack_one(L, wb_cast(wb), index, depth);
        }
    }
}

/* override pack_from */
static void foreign_pack_from(lua_State *L, struct foreign_write_block *b, int from) {
	int n = lua_gettop(L) - from;
	int i;
	for (i=1;i<=n;i++) {
		foreign_pack_one(L, b , from + i, 0);
	}
}

static void
seri(lua_State *L, struct block *b, int len) {
	uint8_t * buffer = (uint8_t*)skynet_malloc(len);
	uint8_t * ptr = buffer;
	int sz = len;
	while(len>0) {
		if (len >= BLOCK_SIZE) {
			memcpy(ptr, b->buffer, BLOCK_SIZE);
			ptr += BLOCK_SIZE;
			len -= BLOCK_SIZE;
			b = b->next;
		} else {
			memcpy(ptr, b->buffer, len);
			break;
		}
	}

	lua_pushlightuserdata(L, buffer);
	lua_pushinteger(L, sz);
}

int foreign_pack(lua_State *L, int mode) {
	struct block temp;
	temp.next = NULL;
	struct foreign_write_block wb;
	foreign_wb_init(&wb, &temp, mode);
	foreign_pack_from(L,&wb,0);
	assert(wb.wb.head == &temp);
	seri(L, &temp, wb.wb.len);

	wb_free(wb_cast(&wb));

    return 2;
}

static int foreign_unpack(lua_State *L, int mode){
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
	return foreign_pack(L, MODE_LUA);
}

static int lluaunpack(lua_State *L) {
	return foreign_unpack(L, MODE_LUA);
}

static int lpack(lua_State *L) {
	return foreign_pack(L, MODE_FOREIGN);
}

static int lunpack(lua_State *L) {
	return foreign_unpack(L, MODE_FOREIGN);
}

static int lremotepack(lua_State *L) {
	return foreign_pack(L, MODE_FOREIGN_REMOTE);
}

static int lremoteunpack(lua_State *L) {
	return foreign_unpack(L, MODE_FOREIGN_REMOTE);
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
