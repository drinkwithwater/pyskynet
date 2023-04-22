#pragma once

#include "foreign_seri/seri.h"

struct block {
	struct block * next;
	char buffer[BLOCK_SIZE];
};

struct write_block {
	struct block * head;
	struct block * current;
	int len;
	int ptr;
};

struct foreign_write_block {
    struct write_block wb;
    int mode;
};

void wb_init(struct write_block *wb , struct block *b);
void wb_free(struct write_block *wb);
void wb_nil(struct write_block *wb);
void wb_boolean(struct write_block *wb, int boolean);
void wb_integer(struct write_block *wb, lua_Integer v);
void wb_real(struct write_block *wb, double v);
void wb_pointer(struct write_block *wb, void *v);
void wb_string(struct write_block *wb, const char *str, int len);
void wb_push(struct write_block *b, const void *buf, int sz);
void wb_real(struct write_block *wb, double v);
int wb_table_array(lua_State *L, struct write_block * wb, int index, int depth);
void wb_table_hash(lua_State *L, struct write_block * wb, int index, int depth, int array_size);
void wb_table_metapairs(lua_State *L, struct write_block *wb, int index, int depth);
void wb_table(lua_State *L, struct write_block *wb, int index, int depth);
void pack_one(lua_State *L, struct write_block *b, int index, int depth);
void pack_from(lua_State *L, struct write_block *b, int from);