# cython: language_level=3

from libc.string cimport memcpy, strcmp
from cpython.pycapsule cimport PyCapsule_New, PyCapsule_GetPointer, PyCapsule_GetName
# import some type check
from cpython.pycapsule cimport PyCapsule_CheckExact
from cpython.unicode cimport PyUnicode_CheckExact, PyUnicode_AsUTF8String, PyUnicode_DecodeUTF8
from cpython.bytes cimport PyBytes_CheckExact, PyBytes_AS_STRING, PyBytes_FromStringAndSize, PyBytes_FromString, PyBytes_GET_SIZE
from cpython.object cimport PyObject, PyTypeObject
from cpython.memoryview cimport PyMemoryView_Check
from cpython.bytearray cimport PyByteArray_AS_STRING
from numpy cimport PyArray_CheckExact
import numpy as np
cimport numpy as cnp
cnp.import_array()

from skynet_py cimport *

DEF BLOCK_SIZE = 128

cdef extern from "skynet_py_foreign_seri_ext.c": #from "lua-foreign_seri.c":
    cdef enum:
        MODE_LUA
        MODE_FOREIGN
        MODE_FOREIGN_REMOTE

    cdef struct block:
        block* next
        char *buffer
    # for read
    uint8_t COMBINE_TYPE(uint8_t, uint8_t)
    cdef struct read_block:
        int mode
    void rb_init(read_block* rb, char* buffer, int size, int mode)
    void* rb_read(read_block* rb, int sz)
    bint rb_get_integer(read_block *rb, int cookie, lua_Integer *pout) except 0
    bint rb_get_real(read_block *rb, double *pout) except 0
    bint rb_get_pointer(read_block *rb, void **pout) except 0
    char *rb_get_string(read_block *rb, uint8_t ahead, size_t *psize) except NULL

    # for write
    cdef struct write_block:
        int len
    cdef struct foreign_write_block:
        write_block wb
        int mode
    void foreign_wb_init(foreign_write_block* wb, block* b, int mode)
    write_block* wb_cast(foreign_write_block* wb);
    void wb_free(write_block *wb)
    void wb_nil(write_block* wb)
    void wb_integer(write_block* wb, lua_Integer v)
    void wb_real(write_block* wb, double v)
    void wb_boolean(write_block* wb, int v)
    void wb_pointer(write_block *wb, void *v)
    void wb_string(write_block *wb, const char *ptr, int sz)
    void foreign_wb_write(foreign_write_block *wb, const void *buf, int sz)
    cdef enum:
        TYPE_NIL
        TYPE_BOOLEAN
        TYPE_NUMBER
        TYPE_NUMBER_ZERO
        TYPE_NUMBER_BYTE
        TYPE_NUMBER_WORD
        TYPE_NUMBER_DWORD
        TYPE_NUMBER_QWORD
        TYPE_NUMBER_REAL

        TYPE_USERDATA
        TYPE_SHORT_STRING
        TYPE_LONG_STRING
        TYPE_TABLE
        TYPE_FOREIGN_USERDATA
    cdef enum:
        MAX_COOKIE
        MAX_DEPTH

    # deal for PyArray & PyMemoryView
    bint PyArray_foreign_check_typechar(object py_obj)
    void wb_foreign_PyArray(foreign_write_block *wb, object arr, object arr_iter)
    object unpack_PyArray(read_block *wb, int cookie)

########################
# functions for unpack #
########################

cdef uint8_t* py_unpack_one(l, read_block *rb, bint in_table) except *:
    cdef uint8_t *aheadptr = <uint8_t*>rb_read(rb, 1)
    cdef uint8_t ahead
    cdef int value_type
    cdef int cookie
    cdef char * strptr = NULL
    cdef size_t strlength = 0
    cdef void * lightuserdata = NULL
    cdef skynet_foreign * foreign_obj = NULL
    cdef lua_Integer intvalue;
    cdef double doublevalue;
    if aheadptr == NULL:
        if in_table:
            raise Exception("invalid stream")
        return NULL
    ahead = aheadptr[0]
    value_type = ahead & 7
    cookie = ahead >> 3
    if value_type == TYPE_NIL:
        l.append(None)
    elif value_type == TYPE_BOOLEAN:
        l.append(cookie>0)
    elif value_type == TYPE_NUMBER:
        if (cookie == TYPE_NUMBER_REAL):
            rb_get_real(rb, &doublevalue)
            l.append(doublevalue)
        else:
            rb_get_integer(rb, cookie, &intvalue)
            l.append(intvalue)
    elif value_type==TYPE_SHORT_STRING or value_type == TYPE_LONG_STRING:
        strptr = rb_get_string(rb, ahead, &strlength)
        l.append(PyBytes_FromStringAndSize(strptr, strlength))
    elif value_type==TYPE_TABLE:
        py_unpack_table(l, rb, cookie)
    elif value_type==TYPE_USERDATA:
        rb_get_pointer(rb, &lightuserdata)
        l.append(PyCapsule_New(lightuserdata, "cptr", NULL))
    elif value_type==TYPE_FOREIGN_USERDATA:
        arr = unpack_PyArray(rb, cookie)
        if arr is None:
            raise Exception("invalid stream when unpacking arr")
        l.append(arr)
    else:
        raise Exception("invalid stream for value type exception")
    return aheadptr

cdef void py_unpack_table(l, read_block *rb, lua_Integer array_size) except *:
    cdef uint8_t value_type
    cdef uint8_t *value_ptr
    cdef int cookie
    if array_size == MAX_COOKIE-1:
        value_ptr = <uint8_t *>rb_read(rb, sizeof(value_type))
        if value_ptr == NULL:
            raise Exception("invalid stream")
        value_type=value_ptr[0]
        cookie = value_type>>3
        if (value_type & 7) != TYPE_NUMBER or cookie == TYPE_NUMBER_REAL:
            raise Exception("invalid stream")
        rb_get_integer(rb, cookie, &array_size)
    #l.append(t)
    next_l = []
    for i in range(1, array_size+1):
        py_unpack_one(next_l, rb, 1)
    next_t = {}
    while True:
        py_unpack_one(next_l, rb, 1)
        if next_l[-1] is None:
            next_l.pop()
            break
        py_unpack_one(next_l, rb, 1)
        next_t[next_l[-2]] = next_l[-1]
        next_l.pop()
        next_l.pop()
    if not next_t:
        l.append(next_l)
    else:
        for i, v in enumerate(next_l):
            next_t[i + 1] = v
        l.append(next_t)

cdef void cunpack(l, char *msg, size_t size, int mode) except *:
    cdef read_block rb
    rb_init(&rb, msg, size, mode);
    cdef int i = 0
    cdef uint8_t value_type = 0
    cdef uint8_t *value_ptr = NULL
    while True:
        if py_unpack_one(l, &rb, 0) == NULL:
            break

# extern
cdef py_foreign_unpack(int mode, capsule_or_bytes, py_sz):
    cdef const char *name
    cdef char *ptr
    cdef size_t sz
    l = []
    if PyCapsule_CheckExact(capsule_or_bytes):
        name = PyCapsule_GetName(capsule_or_bytes)
        ptr = <char *>PyCapsule_GetPointer(capsule_or_bytes, name)
        sz = py_sz
        if strcmp(name, "cptr") == 0 or strcmp(name, "pyptr") == 0:
            cunpack(l, ptr, sz, mode)
            return tuple(l)
        else:
            raise Exception("capsule unpack failed for name = %s " % PyBytes_FromString(name))
    elif PyBytes_CheckExact(capsule_or_bytes):
        ptr = <char *>PyBytes_AS_STRING(capsule_or_bytes)
        sz = PyBytes_GET_SIZE(capsule_or_bytes)
        cunpack(l, ptr, sz, mode)
        return tuple(l)
    else:
        raise Exception("Unexcept type %s " % str(type(capsule_or_bytes)))

######################
# functions for pack #
######################
cdef void py_pack_list(foreign_write_block* wb, list_obj, int depth) except *:
    cdef int array_size = len(list_obj)
    cdef uint8_t n
    if array_size >= MAX_COOKIE - 1:
        n = COMBINE_TYPE(TYPE_TABLE, MAX_COOKIE - 1);
        foreign_wb_write(wb, &n, 1);
        wb_integer(wb_cast(wb), array_size)
    else:
        n = COMBINE_TYPE(TYPE_TABLE, array_size);
        foreign_wb_write(wb, &n, 1);
    for v in list_obj:
        py_pack_one(wb,v,depth)
    wb_nil(wb_cast(wb))

cdef void py_pack_dict(foreign_write_block* wb, dict_obj, int depth) except *:
    cdef uint8_t n = COMBINE_TYPE(TYPE_TABLE, 0);
    foreign_wb_write(wb, &n, 1);
    for k, v in dict_obj.items():
        py_pack_one(wb,k,depth)
        py_pack_one(wb,v,depth)
    wb_nil(wb_cast(wb))

cdef void py_pack_one(foreign_write_block* wb, py_arg, int depth) except *:
    cdef int bytes_sz = 0
    cdef char *bytes_ptr = NULL
    cdef const char *name = NULL
    if depth > MAX_DEPTH:
        raise Exception("serialize can't pack too depth table")
    if py_arg is None:
        wb_nil(wb_cast(wb))
    elif PyCapsule_CheckExact(py_arg):
        name = PyCapsule_GetName(py_arg)
        if strcmp(name, "cptr") == 0:
            wb_pointer(wb_cast(wb), PyCapsule_GetPointer(py_arg, "cptr"))
        else:
            raise Exception("unexception capsule")
    elif PyArray_CheckExact(py_arg):
        if not PyArray_foreign_check_typechar(py_arg):
            raise Exception("unexception typechar %s"%py_arg.dtype.char)
        if wb.mode == MODE_FOREIGN:
            wb_foreign_PyArray(wb, py_arg, None)
        elif wb.mode == MODE_FOREIGN_REMOTE:
            arr_iter = py_arg.flat
            wb_foreign_PyArray(wb, py_arg, arr_iter)
        else:
            raise Exception("seri wb foreign unexception mode")
    elif isinstance(py_arg, dict):
        py_pack_dict(wb, py_arg, depth+1)
    elif isinstance(py_arg, list):
        py_pack_list(wb, py_arg, depth+1)
    else:
        py_arg_type = type(py_arg)
        if py_arg_type == int or np.issubdtype(py_arg_type, np.integer):
            wb_integer(wb_cast(wb), py_arg)
        elif py_arg_type == float or np.issubdtype(py_arg_type, np.floating):
            wb_real(wb_cast(wb), py_arg)
        elif py_arg_type == bool:
            wb_boolean(wb_cast(wb), py_arg)
        elif py_arg_type == bytes:
            bytes_sz = len(py_arg)
            bytes_ptr = py_arg
            wb_string(wb_cast(wb), bytes_ptr, bytes_sz)
        elif py_arg_type == bytearray:
            bytes_sz = len(py_arg)
            bytes_ptr = PyByteArray_AS_STRING(py_arg)
            wb_string(wb_cast(wb), bytes_ptr, bytes_sz)
        elif py_arg_type == str:
            py_arg_bytes = PyUnicode_AsUTF8String(py_arg)
            bytes_sz = len(py_arg_bytes)
            bytes_ptr = py_arg_bytes
            wb_string(wb_cast(wb), bytes_ptr, bytes_sz)
        else:
            raise Exception("Unsupport type %s to serialize"%str(py_arg_type))

cdef py_foreign_pack(int mode, argtuple):
    cdef block temp
    temp.next = NULL
    cdef foreign_write_block wb
    foreign_wb_init(&wb, &temp, mode)
    for one in argtuple:
        py_pack_one(&wb, one, 0)
    # seri
    cdef block *b = &temp
    cdef int length = wb.wb.len
    cdef uint8_t* buffer = <uint8_t*> skynet_malloc(length)
    cdef uint8_t* ptr = buffer
    cdef int sz = length
    while(length > 0):
        if length >= BLOCK_SIZE:
            memcpy(ptr, b.buffer, BLOCK_SIZE)
            ptr += BLOCK_SIZE
            length -= BLOCK_SIZE
            b = b.next
        else:
            memcpy(ptr, b.buffer, length)
            break
    wb_free(wb_cast(&wb))
    return PyCapsule_New(buffer, "cptr", NULL), sz

#########################
# outside pack & unpack #
#########################

def tobytes(capsule, size_t size):
    cdef char *ptr = <char *>PyCapsule_GetPointer(capsule, PyCapsule_GetName(capsule))
    return ptr[:size]

def luapack(*args):
    return py_foreign_pack(MODE_LUA, args)

def luaunpack(capsule, size=None):
    return py_foreign_unpack(MODE_LUA, capsule, size)

def pack(*args):
    return py_foreign_pack(MODE_FOREIGN, args)

def unpack(capsule, size=None):
    return py_foreign_unpack(MODE_FOREIGN, capsule, size)

def remotepack(*args):
    return py_foreign_pack(MODE_FOREIGN_REMOTE, args)

def remoteunpack(capsule, size=None):
    return py_foreign_unpack(MODE_FOREIGN_REMOTE, capsule, size)

def trash(capsule, size_t sz):
    cdef void *ptr = PyCapsule_GetPointer(capsule, "cptr")
    skynet_free(ptr)
