
#include "foreign_seri/read_block.h"

void rb_init(struct read_block * rb, char * buffer, int size, int mode) {
	rb->buffer = buffer;
	rb->len = size;
	rb->ptr = 0;
    rb->mode = mode;
}

void * rb_read(struct read_block *rb, int sz) {
	if (rb->len < sz) {
		return NULL;
	}
	int ptr = rb->ptr;
	rb->ptr += sz;
	rb->len -= sz;
	return rb->buffer + ptr;
}

bool rb_get_integer(struct read_block *rb, int cookie, lua_Integer*pout) {
	switch (cookie) {
	case TYPE_NUMBER_ZERO:
		*pout = 0;
		return true;
	case TYPE_NUMBER_BYTE: {
		uint8_t *pn = (uint8_t *)rb_read(rb,sizeof(uint8_t));
		if (pn == NULL) {
			return false;
		}
		*pout = *pn;
		return true;
	}
	case TYPE_NUMBER_WORD: {
		uint16_t *pn = (uint16_t *)rb_read(rb,sizeof(uint16_t));
		if (pn == NULL) {
			return false;
		}
		*pout = *pn;
		return true;
	}
	case TYPE_NUMBER_DWORD: {
		int32_t *pn = (int32_t *)rb_read(rb,sizeof(int32_t));
		if (pn == NULL) {
			return false;
		}
		*pout = *pn;
		return true;
	}
	case TYPE_NUMBER_QWORD: {
		int64_t *pn = (int64_t *)rb_read(rb,sizeof(int64_t));
		if (pn == NULL) {
			return false;
		}
		*pout = *pn;
		return true;
	}
	default:
		return false;
	}
}

bool rb_get_real(struct read_block *rb, double *pout) {
	double *pn = (double *)rb_read(rb,sizeof(double));
	if (pn == NULL) {
		return false;
	}
	*pout = *pn;
	return true;
}

bool rb_get_pointer(struct read_block *rb, void **pout) {
	void ** v = (void **)rb_read(rb,sizeof(void*));
	if (v == NULL) {
		return false;
	}
	*pout = *v;
	return true;
}

char *rb_get_string(struct read_block *rb, uint8_t ahead, size_t *psize) {
	int type = ahead & 0x7;
	int cookie = ahead >> 3;
	if (type == TYPE_SHORT_STRING) {
		*psize = cookie;
		return rb_read(rb, cookie);
	} else if (cookie == 2) {
		uint16_t *plen = (uint16_t *)rb_read(rb, 2);
		if (plen == NULL) {
			return NULL;
		}
		*psize = *plen;
		return rb_read(rb, *plen);
	} else if (cookie == 4) {
		uint32_t *plen = (uint32_t *)rb_read(rb, 4);
		if (plen == NULL) {
			return NULL;
		}
		*psize = *plen;
		return rb_read(rb, *plen);
	} else {
		return NULL;
	}
}

inline static bool foreign_rb_uint(struct read_block* rb, npy_intp *value) {
	npy_intp result = 0;
	for (uint32_t shift = 0; shift <= 63; shift += 7) {
		uint8_t *p_byte = (uint8_t*)rb_read(rb, 1);
		if(p_byte==NULL) {
			return false;
		}
		npy_intp byte = p_byte[0];
		if (byte & 128) {
			// More bytes are present
			result |= ((byte & 127) << shift);
		} else {
			result |= (byte << shift);
			break;
		}
	}
	if(result < 0) {
		return false;
	}
	*value = result;
	return true;
}

struct numsky_ndarray* rb_get_nsarr(struct read_block *rb, int nd) {
	// 1. get dtype
	char * p_typechar = (char *)rb_read(rb, 1);
	if(p_typechar == NULL){
		return NULL;
	}
	// 2. init from dimensions
	struct numsky_ndarray *arr = numsky_ndarray_precreate(nd, p_typechar[0]);
	for(int i=0;i<nd;i++){
		bool ok = foreign_rb_uint(rb, &arr->dimensions[i]);
		if(!ok) {
			numsky_ndarray_destroy(arr);
			return NULL;
		}
	}
	// 3. build
	struct skynet_foreign *foreign_base;
	char *dataptr;
	if(rb->mode==MODE_FOREIGN) {
		numsky_ndarray_autocount(arr);
		npy_intp *strides = (npy_intp*)rb_read(rb, sizeof(npy_intp)*nd);
		if(strides == NULL) {
			numsky_ndarray_destroy(arr);
			return NULL;
		}
		// 4. get strides,
		for(int i=0;i<nd;i++){
			arr->strides[i] = strides[i];
		}
		// 5. foreign_base, dataptr
		// get foreign_base
		void **v = (void **)rb_read(rb,sizeof(foreign_base));
		if (v == NULL) {
			numsky_ndarray_destroy(arr);
			return NULL;
		}
		memcpy(&foreign_base, v, sizeof(foreign_base));
		if(foreign_base == NULL) {
			numsky_ndarray_destroy(arr);
			printf("can't transfor numsky.ndarray with foreign_base == NULL\n");
			return NULL;
		}
		// get dataptr
		v = (void **)rb_read(rb,sizeof(dataptr));
		if (v == NULL) {
			numsky_ndarray_destroy(arr);
			return NULL;
		}
		memcpy(&dataptr, v, sizeof(dataptr));
	} else if(rb->mode==MODE_FOREIGN_REMOTE) {
		numsky_ndarray_autostridecount(arr);
		// 4. alloc foreign_base
		size_t datasize = arr->count*arr->dtype->elsize;
		// 1) read & copy
		char * pdata = (char*)rb_read(rb, datasize);
		if(pdata==NULL){
			numsky_ndarray_destroy(arr);
			return NULL;
		}
		foreign_base = skynet_foreign_newbytes(datasize);
		dataptr = foreign_base->data;
		memcpy(dataptr, pdata, datasize);
	} else {
		numsky_ndarray_destroy(arr);
		return NULL;
	}
	numsky_ndarray_refdata(arr, foreign_base, dataptr);
	return arr;
}