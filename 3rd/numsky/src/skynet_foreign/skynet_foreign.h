
#pragma once

#include "skynet_foreign/hook_pyskynet.h"

#include <stdbool.h>
#include <stdint.h>

#define SF_METANAME "skynet.foreign"
#define SF_FLAGS_WRITEABLE 1

struct skynet_foreign {
#ifdef BUILD_FOR_PYSKYNET // for python
	struct spinlock lock;
	void *ref_obj; // null for bytes __data, not-null for PyObject*
#endif
    uint8_t flags;
    int ref_count;
	char *data;
	int64_t __data[0];
};

/**********
 * init *
**********/
static inline struct skynet_foreign* skynet_foreign_newbytes(size_t data_size) {
	struct skynet_foreign *obj = (struct skynet_foreign*)foreign_malloc(sizeof(struct skynet_foreign) + data_size);
    SPIN_INIT(obj);
	obj->flags = SF_FLAGS_WRITEABLE;
    obj->ref_count = 1;
#ifdef BUILD_FOR_PYSKYNET // for python
	obj->ref_obj = NULL;
#endif
	obj->data = (char*)obj->__data;
	return obj;
}

#ifdef BUILD_FOR_PYSKYNET // for python
// steal pyobj's ref
static inline struct skynet_foreign* skynet_foreign_newrefpy(void *pyobj, char *data, uint8_t flags) {
	struct skynet_foreign *obj = (struct skynet_foreign*)foreign_malloc(sizeof(struct skynet_foreign));
    SPIN_INIT(obj);
    obj->flags = flags;
	obj->ref_count = 1;
	obj->ref_obj = pyobj;
	obj->data = data;
	return obj;
}

// drop ptr for foreign serialized message
static inline void skynet_foreign_dropseri(void *seri_ptr, size_t sz) {
	printf("drop seri TODO\n");
}

#endif


void skynet_foreign_incref(struct skynet_foreign *obj);

void skynet_foreign_decref(struct skynet_foreign *obj);

