#pragma once

#include "foreign_seri/seri.h"

struct read_block {
	char * buffer;
	int64_t len;
	int64_t ptr;
    int mode;
};

void invalid_stream_line(lua_State *L, struct read_block *rb, int line);

void rb_init(struct read_block * rb, char * buffer, int size, int mode);
void *rb_read(struct read_block *rb, int sz);
lua_Integer get_integer(lua_State *L, struct read_block *rb, int cookie);
double get_real(lua_State *L, struct read_block *rb);
void * get_pointer(lua_State *L, struct read_block *rb);
void get_buffer(lua_State *L, struct read_block *rb, int len);
void unpack_one(lua_State *L, struct read_block *rb);
struct numsky_ndarray* rb_get_nsarr(struct read_block *rb, int nd);

void lrb_unpack_table(lua_State *L, struct read_block *rb, int array_size);
uint8_t* lrb_unpack_one(lua_State *L, struct read_block *rb, bool in_table);

#define invalid_stream(L,rb) invalid_stream_line(L,rb,__LINE__)