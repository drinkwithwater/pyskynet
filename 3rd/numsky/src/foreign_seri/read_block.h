#pragma once

#include "foreign_seri/seri.h"

struct read_block {
	char * buffer;
	int len;
	int ptr;
};

struct foreign_read_block {
    struct read_block rb;
    int mode;
};

void rball_init(struct read_block * rb, char * buffer, int size);
void *rb_read(struct read_block *rb, int sz);
lua_Integer get_integer(lua_State *L, struct read_block *rb, int cookie);
double get_real(lua_State *L, struct read_block *rb);
void * get_pointer(lua_State *L, struct read_block *rb);
void get_buffer(lua_State *L, struct read_block *rb, int len);
void unpack_one(lua_State *L, struct read_block *rb);
void unpack_table(lua_State *L, struct read_block *rb, int array_size);
void push_value(lua_State *L, struct read_block *rb, int type, int cookie);