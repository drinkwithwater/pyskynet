
#include "foreign_seri/write_block.h"

void wb_write(struct write_block *wb, const void *data, int64_t sz) {
	int64_t newCapacity = wb->capacity;
	while (newCapacity < wb->len + sz) {
		newCapacity *= 2;
	}
	if (newCapacity != wb->capacity) {
		char *newBuffer = skynet_malloc(newCapacity);
		memcpy(newBuffer, wb->buffer, wb->len);
		skynet_free(wb->buffer);
		wb->buffer = newBuffer;
		wb->capacity = newCapacity;
	}
	memcpy(wb->buffer + wb->len, data, sz);
	wb->len += sz;
}

void wb_init(struct write_block *wb, int mode) {
	const int BLOCK_SIZE = 128;
	wb->buffer = skynet_malloc(BLOCK_SIZE);
	wb->capacity = BLOCK_SIZE;
	wb->len = 0;
	wb->mode = mode;
}

void wb_free(struct write_block *wb) {
	if(wb->buffer) {
		skynet_free(wb->buffer);
		wb->buffer = NULL;
	}
}

void wb_nil(struct write_block *wb) {
	uint8_t n = TYPE_NIL;
	wb_write(wb, &n, 1);
}

void wb_boolean(struct write_block *wb, int boolean) {
	uint8_t n = COMBINE_TYPE(TYPE_BOOLEAN , boolean ? 1 : 0);
	wb_write(wb, &n, 1);
}

void wb_put_integer(struct write_block *wb, lua_Integer v) {
	int type = TYPE_NUMBER;
	if (v == 0) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_ZERO);
		wb_write(wb, &n, 1);
	} else if (v != (int32_t)v) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_QWORD);
		int64_t v64 = v;
		wb_write(wb, &n, 1);
		wb_write(wb, &v64, sizeof(v64));
	} else if (v < 0) {
		int32_t v32 = (int32_t)v;
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_DWORD);
		wb_write(wb, &n, 1);
		wb_write(wb, &v32, sizeof(v32));
	} else if (v<0x100) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_BYTE);
		wb_write(wb, &n, 1);
		uint8_t byte = (uint8_t)v;
		wb_write(wb, &byte, sizeof(byte));
	} else if (v<0x10000) {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_WORD);
		wb_write(wb, &n, 1);
		uint16_t word = (uint16_t)v;
		wb_write(wb, &word, sizeof(word));
	} else {
		uint8_t n = COMBINE_TYPE(type , TYPE_NUMBER_DWORD);
		wb_write(wb, &n, 1);
		uint32_t v32 = (uint32_t)v;
		wb_write(wb, &v32, sizeof(v32));
	}
}

void wb_put_real(struct write_block *wb, double v) {
	uint8_t n = COMBINE_TYPE(TYPE_NUMBER , TYPE_NUMBER_REAL);
	wb_write(wb, &n, 1);
	wb_write(wb, &v, sizeof(v));
}

void wb_put_pointer(struct write_block *wb, void *v) {
	uint8_t n = TYPE_USERDATA;
	wb_write(wb, &n, 1);
	wb_write(wb, &v, sizeof(v));
}

void wb_put_string(struct write_block *wb, const char *str, int len) {
	if (len < MAX_COOKIE) {
		uint8_t n = COMBINE_TYPE(TYPE_SHORT_STRING, len);
		wb_write(wb, &n, 1);
		if (len > 0) {
			wb_write(wb, str, len);
		}
	} else {
		uint8_t n;
		if (len < 0x10000) {
			n = COMBINE_TYPE(TYPE_LONG_STRING, 2);
			wb_write(wb, &n, 1);
			uint16_t x = (uint16_t) len;
			wb_write(wb, &x, 2);
		} else {
			n = COMBINE_TYPE(TYPE_LONG_STRING, 4);
			wb_write(wb, &n, 1);
			uint32_t x = (uint32_t) len;
			wb_write(wb, &x, 4);
		}
		wb_write(wb, str, len);
	}
}
