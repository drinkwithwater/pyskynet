#pragma once

#include "foreign_seri/seri.h"

struct write_block {
	char *buffer;
	int64_t capacity;
	int64_t len;
    int mode;
};

void wb_init(struct write_block *wb, int mode);
void wb_free(struct write_block *wb);
void wb_write(struct write_block *b, const void *buf, int64_t sz);

void wb_nil(struct write_block *wb);
void wb_boolean(struct write_block *wb, int boolean);

void wb_put_integer(struct write_block *wb, lua_Integer v);
void wb_put_real(struct write_block *wb, double v);
void wb_put_pointer(struct write_block *wb, void *v);
void wb_put_string(struct write_block *wb, const char *str, int len);