
#include "skynet_foreign/skynet_foreign.h"
#include "skynet_foreign/numsky.h"
#include "Python.h"
#include "numpy/arrayobject.h"

#include "foreign_seri/lua-foreign_seri.c"

static bool PyArray_foreign_check_typechar(PyObject *py_obj){
    PyArrayObject *arr = (PyArrayObject*)(py_obj);
	char type = PyArray_DESCR(arr)->type;
    for(int i=0;i<sizeof(NS_DTYPE_CHARS);i++) {
        if(NS_DTYPE_CHARS[i] == type) {
			return true;
		}
	}
	return false;
}

static void foreign_capsule_destructor(PyObject* capsule) {
    struct skynet_foreign *ptr = (struct skynet_foreign*)PyCapsule_GetPointer(capsule, SF_METANAME);
    skynet_foreign_decref(ptr);
}

// array, skynet to python
static PyObject *rb_get_PyArray(struct read_block *rb, int cookie) {
	struct numsky_ndarray *ns_arr = rb_get_nsarr(rb, cookie);
	if(ns_arr == NULL) {
		Py_INCREF(Py_None);
		return ((PyObject *)Py_None);
	}
	PyObject *base = PyCapsule_New(ns_arr->foreign_base, SF_METANAME, foreign_capsule_destructor);
	PyArrayObject *arr = (PyArrayObject *)PyArray_NewFromDescr(&PyArray_Type, PyArray_DescrFromType(ns_arr->dtype->type_num),
			ns_arr->nd, ns_arr->dimensions, ns_arr->strides, ns_arr->dataptr, 0 | NPY_ARRAY_WRITEABLE, NULL);
	PyArray_SetBaseObject(arr, base);
	numsky_ndarray_refdata(ns_arr, NULL, NULL);
	numsky_ndarray_destroy(ns_arr);
	return (PyObject*)arr;
}

static void wb_put_PyArray(struct write_block *wb, PyObject *py_obj, PyObject *py_arr_iter) {
    PyArrayObject *arr = (PyArrayObject*)(py_obj);
	int nd = PyArray_NDIM(arr);
	if(nd >= MAX_COOKIE) {
		printf("nd > 31 in python, error TODO\n");
	}
	// 1. nd & type
	uint8_t n = COMBINE_TYPE(TYPE_FOREIGN_USERDATA, nd);
	wb_write(wb, &n, 1);
	// 2. typechar
	char typechar = PyArray_DESCR(arr)->type;
	wb_write(wb, &typechar, 1);
	// 3. dimension
    for(int i=0;i<nd;i++) {
		foreign_wb_uint(wb, PyArray_DIMS(arr)[i]);
	}
	if(wb->mode == MODE_FOREIGN) {
		// 4. strides
		wb_write(wb, PyArray_STRIDES(arr), sizeof(npy_intp)*nd);
		// 5. foreign_base & dataptr
		Py_INCREF(py_obj);
		struct skynet_foreign* foreign_base = skynet_foreign_newrefpy(py_obj, PyArray_DATA(arr), SF_FLAGS_WRITEABLE);
		wb_write(wb, &foreign_base, sizeof(foreign_base));
		wb_write(wb, &(foreign_base->data), sizeof(foreign_base->data));
	} else if(wb->mode == MODE_FOREIGN_REMOTE) {
		//  value seri
		PyArrayIterObject *arr_iter = (PyArrayIterObject*)(py_arr_iter);
		// 4. data
		int itemsize = PyArray_ITEMSIZE(arr);
		while(PyArray_ITER_NOTDONE(arr_iter)) {
			void *ptr=PyArray_ITER_DATA(arr_iter);
			wb_write(wb, ptr, itemsize);
			PyArray_ITER_NEXT(arr_iter);
		}
	}
}