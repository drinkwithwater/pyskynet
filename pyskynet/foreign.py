
import pyskynet.skynet_py_foreign_seri as foreign_seri
import pyskynet.skynet as skynet
import pyskynet.skynet_py_mq as skynet_py_mq


PTYPE_FOREIGN = skynet_py_mq.PTYPE_FOREIGN
PTYPE_FOREIGN_REMOTE = skynet_py_mq.PTYPE_FOREIGN_REMOTE


class CMDDispatcher(object):
    def __init__(self):
        self.cmd2func = {}

    def __call__(self, first, *args):
        return self.cmd2func[first](*args)

    def __setitem__(self, k, v):
        if isinstance(k, str):
            self.cmd2func[k] = v
            self.cmd2func[k.encode("ascii")] = v
        elif isinstance(k, bytes):
            self.cmd2func[k] = v
            self.cmd2func[k.decode("ascii")] = v
        else:
            self.cmd2func[k] = v

    def __getitem__(self, k):
        return self.cmd2func[k]


CMD = CMDDispatcher()

pack = foreign_seri.pack
unpack = foreign_seri.unpack
remotepack = foreign_seri.remotepack
remoteunpack = foreign_seri.remoteunpack

trash = foreign_seri.trash


def __foreign_dispatch(session, source, argtuple):
    ret = CMD(*argtuple)
    if session != 0:
        if type(ret) == tuple:
            msg_ptr, msg_size = foreign_seri.pack(*ret)
            skynet.ret(msg_ptr, msg_size)
        else:
            msg_ptr, msg_size = foreign_seri.pack(ret)
            skynet.ret(msg_ptr, msg_size)


def __foreign_remote_dispatch(session, source, argtuple):
    ret = CMD(*argtuple)
    if session != 0:
        if type(ret) == tuple:
            skynet.ret(*foreign_seri.remotepack(*ret))
        else:
            skynet.ret(*foreign_seri.remotepack(ret))

def __dontpackhere():
    raise skynet.PySkynetCallException("don't use pack here")

# dispatch foreign message
skynet.register_protocol(
        id=PTYPE_FOREIGN,
        name="foreign",
        pack=__dontpackhere,
        unpack=foreign_seri.unpack,
        dispatch=__foreign_dispatch,
        )

# dispatch foreign message
skynet.register_protocol(
        id=PTYPE_FOREIGN_REMOTE,
        name="foreign_remote",
        pack=__dontpackhere,
        unpack=foreign_seri.remoteunpack,
        dispatch=__foreign_remote_dispatch,
        )


def dispatch(cmd=None, func=None):
    global CMD

    def wrapper(func):
        CMD[cmd] = func
        return func
    if not (func is None):
        assert callable(func), "dispatch's second arg must be callable"
        CMD[cmd] = func
    elif callable(cmd):
        CMD = func
    elif isinstance(cmd, dict):
        for k, v in cmd.items():
            CMD[k] = v
    elif isinstance(cmd, str) or isinstance(cmd, bytes):
        return wrapper
    else:
        raise Exception("dispatch failed for unexception args")


def call(addr, *args):
    return foreign_seri.unpack(*skynet.rawcall(
        addr, PTYPE_FOREIGN, *foreign_seri.pack(*args)))


def send(addr, *args):
    return skynet.rawsend(addr, PTYPE_FOREIGN, *foreign_seri.pack(*args))
