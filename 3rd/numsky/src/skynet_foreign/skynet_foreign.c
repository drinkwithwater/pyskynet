
#include "skynet_foreign/skynet_foreign.h"
#ifdef BUILD_FOR_PYSKYNET
#include "skynet_modify/skynet_py.h" // for include skynet_py_decref_python
#endif

#include <stdio.h>

static inline void skynet_foreign_delete(struct skynet_foreign *obj){
#ifdef BUILD_FOR_PYSKYNET
    if(obj->ref_obj != NULL) {
        skynet_py_decref_python(obj->ref_obj);
    }
#endif
    SPIN_DESTROY(obj);
    foreign_free(obj);
}

void skynet_foreign_incref(struct skynet_foreign *obj) {
	SPIN_LOCK(obj)
    obj->ref_count ++;
    SPIN_UNLOCK(obj)
}

void skynet_foreign_decref(struct skynet_foreign *obj) {
    bool noref = 0;
	SPIN_LOCK(obj)
    obj->ref_count --;
    noref = (obj->ref_count <= 0);
    SPIN_UNLOCK(obj)
    if(noref) {
        if(obj->ref_count < 0){
            printf("ERROR!!!!!, foreign refcount < 0\n");
        }
		skynet_foreign_delete(obj);
    }
}

