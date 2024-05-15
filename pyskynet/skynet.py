
import pyskynet._core as _core
import pyskynet._foreign_seri as foreign_seri
import gevent
from gevent.event import AsyncResult, Event
import traceback
import logging

PTYPE_TEXT = _core.SKYNET_PTYPE.PTYPE_TEXT
PTYPE_CLIENT = _core.SKYNET_PTYPE.PTYPE_CLIENT
PTYPE_SOCKET = _core.SKYNET_PTYPE.PTYPE_SOCKET
PTYPE_LUA = _core.SKYNET_PTYPE.PTYPE_LUA

SKYNET_PTYPE = _core.SKYNET_PTYPE

#####################
# py<->skynet proto #
#####################

local_session_to_ar = {}

pyskynet_proto_dict = {}

logger = logging.getLogger("pyskynet")

default_logger_handler = logging.StreamHandler()
default_logger_handler.setFormatter(logging.Formatter("[%(asctime)s][%(levelname)s]%(pathname)s:%(lineno)d - %(message)s"))

logger.addHandler(default_logger_handler)
logger.setLevel(logging.INFO)

class PySkynetProto(object):
    def __init__(self, id, name, pack=None, unpack=None, dispatch=None):
        self.id = id
        self.name = name
        self.pack = pack
        self.unpack = unpack
        self.dispatch = dispatch


# skynet.lua
def register_protocol(id, name, pack=None, unpack=None, dispatch=None):
    if type(name) == bytes:
        name = name.decode()
    assert id not in pyskynet_proto_dict, "pyskynet proto id=%s existed" % id
    assert name not in pyskynet_proto_dict, "pyskynet proto name=%s existed" % name
    assert type(name) == str and type(id) == int and \
    id >= 0 and id <= 255, "pyskynet proto register failed id=%s, name=%s" % (id, name)
    psproto = PySkynetProto(id, name, pack, unpack, dispatch)
    pyskynet_proto_dict[id] = psproto
    pyskynet_proto_dict[name] = psproto


# skynet.lua
def _error_dispatch(error_session, error_source, arg):
    co = gevent.getcurrent()
    co_to_remote_session.pop(co)
    co_to_remote_address.pop(co)
    local_session_to_ar[error_session].set(None)


# skynet.lua
register_protocol(
        id=SKYNET_PTYPE.PTYPE_LUA,
        name="lua",
        pack=foreign_seri.luapack,
        unpack=foreign_seri.luaunpack,
        )

# skynet.lua
register_protocol(
        id=SKYNET_PTYPE.PTYPE_ERROR,
        name="error",
        pack=foreign_seri.luapack,
        unpack=lambda a, b: None,
        dispatch=_error_dispatch,
        )


# skynet.lua
def dispatch(name, func):
    """
        dispatch in skynet.lua,
        set dispatch
    """
    pyskynet_proto_dict[name].dispatch = func


####################
# code for session #
####################

co_to_remote_session = {}
co_to_remote_address = {}


class PySkynetCallException(Exception):
    pass


# skynet.lua
def rawcall(dst, type_name_or_id, msg_ptr, msg_size):
    """
        rawcall in skynet.lua, rpc call
    """
    psproto = pyskynet_proto_dict[type_name_or_id]
    session = _core.csend(dst, psproto.id, None, msg_ptr, msg_size)
    if session is None:
        raise PySkynetCallException("send to invalid address %08x" % dst)
    ar = AsyncResult()
    local_session_to_ar[session] = ar
    re = ar.get()
    if re:
        return re
    else:
        if not (schedule_event is None):
            schedule_event.wait()
        raise PySkynetCallException("call failed from %s" % dst)


# skynet.lua
def rawsend(dst, type_name_or_id, msg_ptr, msg_size):
    """
        rawsend in skynet.lua, send don't need ret
    """
    psproto = pyskynet_proto_dict[type_name_or_id]
    return _core.csend(dst, psproto.id, 0, msg_ptr, msg_size)


# skynet.lua
def call(addr, type_name_or_id, *args):
    psproto = pyskynet_proto_dict[type_name_or_id]
    msg_ptr, msg_size = psproto.pack(*args)
    return psproto.unpack(*rawcall(addr, type_name_or_id, msg_ptr, msg_size))


# skynet.lua
def send(addr, type_name_or_id, *args):
    psproto = pyskynet_proto_dict[type_name_or_id]
    msg_ptr, msg_size = psproto.pack(*args)
    return rawsend(addr, type_name_or_id, msg_ptr, msg_size)


# skynet.lua
def ret(ret_msg_ptr, ret_size):
    """
        ret in skynet.lua, return for other's call
    """
    co = gevent.getcurrent()
    session = co_to_remote_session.pop(co)
    source = co_to_remote_address.pop(co)
    if session == 0:
        return False
    else:
        return _core.csend(source, SKYNET_PTYPE.PTYPE_RESPONSE, session, ret_msg_ptr, ret_size) is not None


################
# raw dispatch #
################
def __async_dispatch(source, type_id, session, ptr, length):
    if type_id == SKYNET_PTYPE.PTYPE_RESPONSE:
        # TODO exception
        try:
            ar = local_session_to_ar.pop(session)
            ar.set((ptr, length))
        except KeyError as e:
            logger.error("unknown response session: %d from %x"%(session, source))
    else:
        # TODO exception
        try:
            psproto = pyskynet_proto_dict[type_id]
        except KeyError as e:
            if session != 0:
                _core.csend(source, SKYNET_PTYPE.PTYPE_ERROR, session, "")
            else:
                logger.error("unknown request with unexcept type_id %s, session: %d from %x"%(type_id, session, source))
            return
        co = gevent.getcurrent()
        co_to_remote_session[co] = session
        co_to_remote_address[co] = source
        if callable(psproto.dispatch):
            try:
                psproto.dispatch(session, source, psproto.unpack(ptr, length))
                if co in co_to_remote_session:
                    if session != 0:
                        logger.warn("Maybe forgot response session %s from %s " % (session, source))
                    co_to_remote_session.pop(co)
                    co_to_remote_address.pop(co)
            except Exception as e:
                if session != 0:
                    _core.csend(source, SKYNET_PTYPE.PTYPE_ERROR, session, "")
                logger.error(traceback.format_exc())
                if co in co_to_remote_session:
                    co_to_remote_session.pop(co)
                    co_to_remote_address.pop(co)
        else:
            if session != 0:
                _core.csend(source, SKYNET_PTYPE.PTYPE_ERROR, session, "")
            else:
                logger.error("request with dispatch not callable, session: %d from %x"%(session, address))
            return


boot_result = AsyncResult()

# logger handle
def __handle_logger(source, data):
    data = data.decode("utf-8")
    if data[0] == '|':
        sp = data.split('|')
        level = int(sp[1])
        filename = "(%08x:%s)"%(source, sp[2])
        lineno = int(sp[3])
        body = sp[4] if len(sp) <= 5 else "|".join(sp[4:])
    else:
        level = logging.WARN
        filename = "(%08x:unknown)" % source
        lineno = 0
        body = data
    if logger.isEnabledFor(level):
        record = logging.LogRecord(logger.name, level, filename, lineno, body, None, None, address=source)
        logger.handle(record)

schedule_event = None

def msg_schedule():
    global boot_result
    global schedule_event
    assert schedule_event is None, "there will never be more then one msg schedule running"
    schedule_event = Event()
    while True:
        source, type_id, session, ptr, length = _core.crecv()
        if source is None:
            break
        if type_id == _core.PTYPE_LOGGER_PYTHON:
            try:
                __handle_logger(source, ptr)
            except Exception as e:
                logger.error("handle logger error %s", e)
        elif not (boot_result is None):
            # assert first message ( c.send(".python", 0, 0, "") )
            assert type_id == 0, "first message type must be 0 but get %s" % type_id
            assert session == 0, "first message session must be 0 but get %s" % session
            boot_service, = foreign_seri.luaunpack(ptr, length)
            boot_result.set(boot_service)
            boot_result = None
        else:
            gevent.spawn(__async_dispatch, source, type_id, session, ptr, length)
    schedule_event.set()
    schedule_event = None

