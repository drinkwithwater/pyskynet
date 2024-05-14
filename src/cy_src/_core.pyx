# cython: language_level=3
# cython: legacy_implicit_noexcept=True

import _cffi_backend
from cpython.pycapsule cimport PyCapsule_GetPointer, PyCapsule_New, PyCapsule_CheckExact, PyCapsule_SetPointer, PyCapsule_GetName
from cpython.bytes cimport PyBytes_FromString, PyBytes_FromStringAndSize, PyBytes_CheckExact
from cpython.unicode cimport PyUnicode_CheckExact, PyUnicode_AsUTF8String
from cpython.bytes cimport PyBytes_CheckExact, PyBytes_AS_STRING
from libc.string cimport memcpy, strcmp
from cpython.ref cimport Py_XDECREF, PyObject
from libc.stdio cimport sscanf, sprintf

cdef extern from "skynet.h":
    ctypedef int int64_t
    ctypedef int int32_t
    ctypedef int uint32_t
    ctypedef int uint16_t
    ctypedef int uint8_t
    void skynet_free(void *)
    cdef enum:
        PTYPE_TEXT
        PTYPE_RESPONSE
        PTYPE_MULTICAST
        PTYPE_CLIENT
        PTYPE_SYSTEM
        PTYPE_HARBOR
        PTYPE_SOCKET
        PTYPE_ERROR
        PTYPE_RESERVED_QUEUE
        PTYPE_RESERVED_DEBUG
        PTYPE_RESERVED_LUA
        PTYPE_RESERVED_SNAX
        PTYPE_TAG_ALLOCSESSION
        PTYPE_TAG_DONTCOPY
    int skynet_send(skynet_context* ctx, uint32_t src, uint32_t dst, int type, int session, void* msg, size_t sz);
    int skynet_sendname(skynet_context* ctx, uint32_t src, const char *dst, int type, int session, void* msg, size_t sz);

cdef extern from "skynet_modify/skynet_modify.h":
    cdef struct skynet_context:
        pass
    cdef struct skynet_config:
        int thread
        int harbor
        int profile
        const char * daemon
        const char * module_path
        const char * bootstrap
    cdef struct SkynetModifyMessage:
        int type
        int session
        uint32_t source
        void * data
        size_t size
    cdef struct SkynetModifyQueue:
        pass
    cdef struct SkynetModifyGlobal:
        SkynetModifyQueue msg_queue
        SkynetModifyQueue ctrl_queue
        uint32_t python_address;
        skynet_context * python_context;
    SkynetModifyGlobal G_SKYNET_MODIFY
    cdef enum:
        PTYPE_FOREIGN_REMOTE
        PTYPE_FOREIGN
        PTYPE_DECREF_PYTHON
        PTYPE_LOGGER_PYTHON
    int skynet_modify_queue_pop(SkynetModifyMessage* message); # return 1 if empty else 0
    void skynet_modify_init(int (*p_uv_async_send)(void *), void* msg_async_t);
    void skynet_modify_start(skynet_config * config)
    void skynet_modify_wakeup();
    int skynet_modify_setlenv(const char *key, const char *value_str, size_t sz)
    const char *skynet_modify_getlenv(const char *key, size_t *sz);
    const char *skynet_modify_nextenv(const char *key)
    const char *skynet_modify_getscript(int index, size_t *sz);
    int skynet_modify_refscript(const char*key, size_t sz);

cdef extern from "skynet_env.h":
    const char * skynet_getenv(const char *key);


ctypedef (char *)(* f_type)(object, object)

def init(ffi, async_send, msg_watcher):
    # because gevent's libuv bind with cffi, so we can get pointer by this way
    cdef void ** _cffi_exports = <void **>PyCapsule_GetPointer(_cffi_backend._C_API, "cffi")
    # get the '_cffi_to_c_pointer' function in _cffi_include.h of cffi
    cdef f_type _cffi_to_c_pointer = <f_type>_cffi_exports[11]
    skynet_modify_init(
            <int (*)(void*)>_cffi_to_c_pointer(async_send, ffi.typeof(async_send)),
            <void *>_cffi_to_c_pointer(msg_watcher, ffi.typeof(msg_watcher))
            )

cdef __check_bytes(s):
    t = type(s)
    if t == str:
        return s.encode("utf-8")
    elif t == bytes:
        return s
    else:
        raise Exception("type %s can't convert to bytes" % str(t))

def setlenv(key, capsule_or_bytes, py_sz=None):
    cdef size_t sz
    cdef const char *ptr
    if PyCapsule_CheckExact(capsule_or_bytes):
        ptr = <char *>PyCapsule_GetPointer(capsule_or_bytes, "cptr")
        sz = py_sz
    elif PyBytes_CheckExact(capsule_or_bytes):
        ptr = capsule_or_bytes
        sz = len(capsule_or_bytes)
    else:
        raise Exception("skynet_py env value must be bytes or pointer")
    key = __check_bytes(key)
    cdef int ret = skynet_modify_setlenv(key, ptr, sz)
    if ret != 0:
        raise Exception("setlenv but key conflict")


def getlenv(key):
    key = __check_bytes(key)
    cdef size_t sz
    cdef const char * value = skynet_modify_getlenv(key, &sz);
    if value != NULL:
        return PyBytes_FromStringAndSize(value, sz);
    else:
        raise Exception("getlenv but value not existed")

def nextenv(key):
    cdef const char * ptr
    if key is None:
        ptr = skynet_modify_nextenv(NULL)
    else:
        key = __check_bytes(key)
        ptr = skynet_modify_nextenv(key)
    if ptr == NULL:
        return None
    else:
        return PyBytes_FromString(ptr)

def refscript(script):
    script = __check_bytes(script)
    index = skynet_modify_refscript(script, len(script))
    return index

def getscript(index):
    cdef size_t sz
    cdef const char * value = skynet_modify_getscript(index, &sz)
    if value != NULL:
        return PyBytes_FromStringAndSize(value, sz);
    else:
        return None

def start(int thread, int profile):
    cdef skynet_config config;
    config.thread = thread
    config.profile = profile
    # use getenv for a stable ptr
    config.module_path = skynet_getenv("cservice")
    config.bootstrap = skynet_getenv("bootstrap")
    # ignore
    config.harbor = 0
    config.daemon = NULL # just ignore daemon
    skynet_modify_start(&config)

# if pyholder not started, return 0
def self():
    return G_SKYNET_MODIFY.python_address;

##########################
# functions for messages #
##########################

cdef void free_pyptr(object capsule):
    cdef void *ptr = PyCapsule_GetPointer(capsule, "pyptr")
    skynet_free(ptr)

# pop message from msg_queue
def crecv():
    cdef SkynetModifyMessage msg
    cdef int ret = skynet_modify_queue_pop(&msg)
    while ret == 0 and msg.type == PTYPE_DECREF_PYTHON:
        Py_XDECREF(<PyObject*>msg.data)
        ret = skynet_modify_queue_pop(&msg)
    cdef char* dataptr = <char*>msg.data
    if ret != 0:
        return None, None, None, None, None
    elif dataptr == NULL:
        return msg.source, msg.type, msg.session, b"", 0
    elif msg.type == PTYPE_LOGGER_PYTHON:
        text = dataptr[:msg.size]
        skynet_free(dataptr)
        return msg.source, msg.type, msg.session, text, 0
    else:
        # TODO when msg.type is error , msg.data will be nil ?
        return msg.source, msg.type, msg.session, PyCapsule_New(dataptr, "pyptr", free_pyptr), msg.size

# see lsend in lua-skynet.c
def csend(py_dst, int type_id, py_session, py_msg, py_size=None):
    assert G_SKYNET_MODIFY.python_address > 0, "skynet threads has not been started yet, call 'pyskynet.start()' first."
    # 1. check dst
    cdef char * dstname = NULL
    cdef int dst = 0
    cdef bytes py_dst_bytes
    if PyBytes_CheckExact(py_dst):
        dstname = py_dst
    elif PyUnicode_CheckExact(py_dst):
        py_dst_bytes = PyUnicode_AsUTF8String(py_dst)
        dstname = py_dst_bytes
    else:
        dst = py_dst
    # 2. check session
    cdef int session = 0
    if py_session is None:
        type_id |= PTYPE_TAG_ALLOCSESSION
        session = 0
    else:
        session = py_session
    # 3. check ptr, size
    cdef char * ptr = NULL
    cdef int size = 0
    cdef bytes py_msg_bytes
    if PyCapsule_CheckExact(py_msg):
        type_id |= PTYPE_TAG_DONTCOPY
        ptr = <char *>PyCapsule_GetPointer(py_msg, "cptr")
        size = py_size
    elif PyBytes_CheckExact(py_msg):
        ptr = py_msg
        size = len(py_msg)
    elif PyUnicode_CheckExact(py_msg):
        py_msg_bytes = PyUnicode_AsUTF8String(py_msg)
    else:
        raise Exception("type:%s unexcept when skynet csend"%type(py_msg))

    if dstname == NULL:
        session = skynet_send(G_SKYNET_MODIFY.python_context, G_SKYNET_MODIFY.python_address, dst, type_id, session, ptr, size)
    else:
        session = skynet_sendname(G_SKYNET_MODIFY.python_context, G_SKYNET_MODIFY.python_address, dstname, type_id, session, ptr, size)
    skynet_modify_wakeup()

    if session < 0:
        if session == -2:
            raise Exception("package is too large:%s"%session)
            return False
        else:
            return None
    else:
        return session

# extern
def tostring(capsule, size_t size):
    b = tobytes(capsule, size)
    return b.decode()

# extern
def tobytes(capsule, size_t size):
    cdef char *name = PyCapsule_GetName(capsule)
    cdef char *ptr = <char *>PyCapsule_GetPointer(capsule, name)
    cdef bytes s
    if strcmp(name, "cptr") == 0 or strcmp(name, "pyptr") == 0:
        s = ptr[:size]
        return s
    else:
        raise Exception("capsule unpack failed for name = %s")

class PTYPEEnum(object):
    def __init__(self):
        self.PTYPE_TEXT=PTYPE_TEXT
        self.PTYPE_RESPONSE=PTYPE_RESPONSE
        self.PTYPE_MULTICAST=PTYPE_MULTICAST
        self.PTYPE_CLIENT=PTYPE_CLIENT
        self.PTYPE_SYSTEM=PTYPE_SYSTEM
        self.PTYPE_HARBOR=PTYPE_HARBOR
        self.PTYPE_SOCKET=PTYPE_SOCKET
        self.PTYPE_ERROR=PTYPE_ERROR
        self.PTYPE_QUEUE=PTYPE_RESERVED_QUEUE
        self.PTYPE_DEBUG=PTYPE_RESERVED_DEBUG
        self.PTYPE_LUA=PTYPE_RESERVED_LUA
        self.PTYPE_SNAX=PTYPE_RESERVED_SNAX
        self.PTYPE_TRACE=12 # TRACE defined in skynet.lua
        self.PTYPE_FOREIGN=PTYPE_FOREIGN
        self.PTYPE_FOREIGN_REMOTE=PTYPE_FOREIGN_REMOTE
        self.PTYPE_LOGGER_PYTHON=PTYPE_LOGGER_PYTHON
        self.PTYPE_TAG_ALLOCSESSION=PTYPE_TAG_ALLOCSESSION
        self.PTYPE_TAG_DONTCOPY=PTYPE_TAG_DONTCOPY


SKYNET_PTYPE_id_to_name = {}
SKYNET_PTYPE_name_to_id = {}
SKYNET_PTYPE = PTYPEEnum()

for key, id in SKYNET_PTYPE.__dict__.items():
    name = key[6:].lower()
    SKYNET_PTYPE_id_to_name[id] = name
    SKYNET_PTYPE_name_to_id[name] = id
    locals()[key] = id

SKYNET_PTYPE_user_builtin_ids = [SKYNET_PTYPE.PTYPE_LUA, SKYNET_PTYPE.PTYPE_CLIENT, SKYNET_PTYPE.PTYPE_SOCKET, SKYNET_PTYPE.PTYPE_TEXT, SKYNET_PTYPE.PTYPE_FOREIGN, SKYNET_PTYPE.PTYPE_FOREIGN_REMOTE]
def user_assert_id_name(id, name):
    is_builtin = False
    if name in SKYNET_PTYPE_name_to_id:
        assert id == SKYNET_PTYPE_name_to_id[name], "skynet proto type name=%s must bind id=%s" % (name, id)
        is_builtin = True
    if id in SKYNET_PTYPE_id_to_name:
        assert name == SKYNET_PTYPE_id_to_name[id], "skynet proto type id=%s must bind name=%s" % (id, name)
        is_builtin = True
    if is_builtin:
        assert id in SKYNET_PTYPE_user_builtin_ids, "skynet proto can only register builtin proto 'lua' or 'client' or 'socket' or 'text' or 'foreign' or 'foreign_remote', but get name=%s" % name

    assert 0 <= id and id <= 255, "skynet proto type id must be less than 256 but get id=%s" % id