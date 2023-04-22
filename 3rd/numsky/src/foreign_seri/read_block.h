#pragma once

#include "foreign_seri/seri.h"

struct read_block {
	char * buffer;
	int64_t len;
	int64_t ptr;
    int mode;
};

inline void invalid_stream_line(lua_State *L, struct read_block *rb, int line) {
	int len = rb->len;
	luaL_error(L, "Invalid serialize stream %d (line:%d)", len, line);
}

#define invalid_stream(L,rb) invalid_stream_line(L,rb,__LINE__)

void rb_init(struct read_block * rb, char * buffer, int size, int mode);
void *rb_read(struct read_block *rb, int sz);
bool rb_get_integer(struct read_block *rb, int cookie, lua_Integer *pout);
bool rb_get_real(struct read_block *rb, double *pout);
bool rb_get_pointer(struct read_block *rb, void **pout);
char *rb_get_string(struct read_block *rb, uint8_t ahead, size_t *psize);
struct numsky_ndarray* rb_get_nsarr(struct read_block *rb, int nd);

void lrb_unpack_table(lua_State *L, struct read_block *rb, lua_Integer array_size);
uint8_t* lrb_unpack_one(lua_State *L, struct read_block *rb, bool in_table);