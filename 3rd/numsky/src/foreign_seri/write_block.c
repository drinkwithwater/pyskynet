
#include "foreign_seri/write_block.h"

inline static struct block *
blk_alloc(void) {
	struct block *b = (struct block*)skynet_malloc(sizeof(struct block));
	b->next = NULL;
	return b;
}

void wb_push(struct write_block *b, const void *buf, int sz) {
	const char * buffer = (const char*)buf;
	if (b->ptr == BLOCK_SIZE) {
_again:
		b->current = b->current->next = blk_alloc();
		b->ptr = 0;
	}
	if (b->ptr <= BLOCK_SIZE - sz) {
		memcpy(b->current->buffer + b->ptr, buffer, sz);
		b->ptr+=sz;
		b->len+=sz;
	} else {
		int copy = BLOCK_SIZE - b->ptr;
		memcpy(b->current->buffer + b->ptr, buffer, copy);
		buffer += copy;
		b->len += copy;
		sz -= copy;
		goto _again;
	}
}

void wb_init(struct write_block *wb , struct block *b) {
	wb->head = b;
	assert(b->next == NULL);
	wb->len = 0;
	wb->current = wb->head;
	wb->ptr = 0;
}

void wb_free(struct write_block *wb) {
	struct block *blk = wb->head;
	blk = blk->next;	// the first block is on stack
	while (blk) {
		struct block * next = blk->next;
		skynet_free(blk);
		blk = next;
	}
	wb->head = NULL;
	wb->current = NULL;
	wb->ptr = 0;
	wb->len = 0;
}

void wb_nil(struct write_block *wb) {
	uint8_t n = TYPE_NIL;
	wb_push(wb, &n, 1);
}

void wb_boolean(struct write_block *wb, int boolean) {
	uint8_t n = COMBINE_TYPE(TYPE_BOOLEAN , boolean ? 1 : 0);
	wb_push(wb, &n, 1);
}

void wb_integer(struct write_block *wb, lua_Integer v) {
	int type = TYPE_NUMBER;
	if (v == 0) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_ZERO);
		wb_push(wb, &n, 1);
	} else if (v != (int32_t)v) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_QWORD);
		int64_t v64 = v;
		wb_push(wb, &n, 1);
		wb_push(wb, &v64, sizeof(v64));
	} else if (v < 0) {
		int32_t v32 = (int32_t)v;
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_DWORD);
		wb_push(wb, &n, 1);
		wb_push(wb, &v32, sizeof(v32));
	} else if (v<0x100) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_BYTE);
		wb_push(wb, &n, 1);
		uint8_t byte = (uint8_t)v;
		wb_push(wb, &byte, sizeof(byte));
	} else if (v<0x10000) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_WORD);
		wb_push(wb, &n, 1);
		uint16_t word = (uint16_t)v;
		wb_push(wb, &word, sizeof(word));
	} else {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_DWORD);
		wb_push(wb, &n, 1);
		uint32_t v32 = (uint32_t)v;
		wb_push(wb, &v32, sizeof(v32));
	}
}

void wb_real(struct write_block *wb, double v) {
	uint8_t n = COMBINE_TYPE(TYPE_NUMBER , TYPE_NUMBER_REAL);
	wb_push(wb, &n, 1);
	wb_push(wb, &v, sizeof(v));
}

void wb_pointer(struct write_block *wb, void *v) {
	uint8_t n = TYPE_USERDATA;
	wb_push(wb, &n, 1);
	wb_push(wb, &v, sizeof(v));
}

void wb_string(struct write_block *wb, const char *str, int len) {
	if (len < MAX_COOKIE) {
		uint8_t n = COMBINE_TYPE(TYPE_SHORT_STRING, len);
		wb_push(wb, &n, 1);
		if (len > 0) {
			wb_push(wb, str, len);
		}
	} else {
		uint8_t n;
		if (len < 0x10000) {
			n = COMBINE_TYPE(TYPE_LONG_STRING, 2);
			wb_push(wb, &n, 1);
			uint16_t x = (uint16_t) len;
			wb_push(wb, &x, 2);
		} else {
			n = COMBINE_TYPE(TYPE_LONG_STRING, 4);
			wb_push(wb, &n, 1);
			uint32_t x = (uint32_t) len;
			wb_push(wb, &x, 4);
		}
		wb_push(wb, str, len);
	}
}

int wb_table_array(lua_State *L, struct write_block * wb, int index, int depth) {
	int array_size = lua_rawlen(L,index);
	if (array_size >= MAX_COOKIE-1) {
		uint8_t n = COMBINE_TYPE(TYPE_TABLE, MAX_COOKIE-1);
		wb_push(wb, &n, 1);
		wb_integer(wb, array_size);
	} else {
		uint8_t n = COMBINE_TYPE(TYPE_TABLE, array_size);
		wb_push(wb, &n, 1);
	}

	int i;
	for (i=1;i<=array_size;i++) {
		lua_rawgeti(L,index,i);
		pack_one(L, wb, -1, depth);
		lua_pop(L,1);
	}

	return array_size;
}

void wb_table_hash(lua_State *L, struct write_block * wb, int index, int depth, int array_size) {
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
		pack_one(L,wb,-2,depth);
		pack_one(L,wb,-1,depth);
		lua_pop(L, 1);
	}
	wb_nil(wb);
}

void wb_table_metapairs(lua_State *L, struct write_block *wb, int index, int depth) {
	uint8_t n = COMBINE_TYPE(TYPE_TABLE, 0);
	wb_push(wb, &n, 1);
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
		pack_one(L, wb, -2, depth);
		pack_one(L, wb, -1, depth);
		lua_pop(L, 1);
	}
	wb_nil(wb);
}

void wb_table(lua_State *L, struct write_block *wb, int index, int depth) {
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	if (index < 0) {
		index = lua_gettop(L) + index + 1;
	}
	if (luaL_getmetafield(L, index, "__pairs") != LUA_TNIL) {
		wb_table_metapairs(L, wb, index, depth);
	} else {
		int array_size = wb_table_array(L, wb, index, depth);
		wb_table_hash(L, wb, index, depth, array_size);
	}
}

void pack_one(lua_State *L, struct write_block *b, int index, int depth) {
	if (depth > MAX_DEPTH) {
		wb_free(b);
		luaL_error(L, "serialize can't pack too depth table");
	}
	int type = lua_type(L,index);
	switch(type) {
	case LUA_TNIL:
		wb_nil(b);
		break;
	case LUA_TNUMBER: {
		if (lua_isinteger(L, index)) {
			lua_Integer x = lua_tointeger(L,index);
			wb_integer(b, x);
		} else {
			lua_Number n = lua_tonumber(L,index);
			wb_real(b,n);
		}
		break;
	}
	case LUA_TBOOLEAN:
		wb_boolean(b, lua_toboolean(L,index));
		break;
	case LUA_TSTRING: {
		size_t sz = 0;
		const char *str = lua_tolstring(L,index,&sz);
		wb_string(b, str, (int)sz);
		break;
	}
	case LUA_TLIGHTUSERDATA:
		wb_pointer(b, lua_touserdata(L,index));
		break;
	case LUA_TTABLE: {
		if (index < 0) {
			index = lua_gettop(L) + index + 1;
		}
		wb_table(L, b, index, depth+1);
		break;
	}
	default:
		wb_free(b);
		luaL_error(L, "Unsupport type %s to serialize", lua_typename(L, type));
	}
}

void pack_from(lua_State *L, struct write_block *b, int from) {
	int n = lua_gettop(L) - from;
	int i;
	for (i=1;i<=n;i++) {
		pack_one(L, b , from + i, 0);
	}
}