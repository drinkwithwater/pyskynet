
# 1. import gevent, gevent must use libuv
import gevent
gevent.config.loop = "libuv"
import os
import sys
import gevent.libuv._corecffi as libuv_cffi
import gevent.event
import gevent.signal

# 2. dlopen with flags RTLD_GLOBAL
flags = sys.getdlopenflags()
sys.setdlopenflags(flags | os.RTLD_GLOBAL)
import pyskynet._core as _core
sys.setdlopenflags(flags)

# 3. some module
import pyskynet.config as config
import pyskynet.foreign as foreign
import pyskynet._foreign_seri
from typing import Dict, Any
import pyskynet.skynet as skynet
import logging

####################
# 1. env set & get #
####################

def getenv(key):
    data = _core.getlenv(key)
    if data is None:
        return None
    else:
        return foreign.remoteunpack(data)[0]


def setenv(key, value):
    if _core.self() != 0:
        assert (key is None) or (getenv(key) is None), "Can't setenv exist key : %s " % key
    msg_ptr, msg_size = foreign.remotepack(value)
    _core.setlenv(key, msg_ptr, msg_size)
    foreign.trash(msg_ptr, msg_size)


def envs():
    key = None
    re = {}
    while True:
        key = _core.nextenv(key)
        if(key is None):
            break
        else:
            re[key] = getenv(key)
    return re


#################
# 2. boot items #
#################

__boot_event = None
__boot_service = None

__msg_watcher = gevent.get_hub().loop.async_()
__ctrl_watcher = gevent.get_hub().loop.async_()

# first callback, waiting for pyskynet_boot
def __first_msg_callback():
    global __boot_service
    source, type_id, session, ptr, length = _core.crecv()
    # assert first message ( c.send(".python", 0, 0, "") )
    assert type_id == 0, "first message type must be 0 but get %s" % type_id
    assert session == 0, "first message session must be 0 but get %s" % session
    __boot_service, = pyskynet._foreign_seri.luaunpack(ptr, length)
    __msg_watcher.callback = lambda: gevent.spawn(skynet.__async_handle)
    gevent.spawn(skynet.__async_handle)
    __boot_event.set()


def log_record(source:int, message:bytes)->logging.LogRecord:
    return logging.LogRecord(skynet.logger.name, logging.INFO, "(unknown lua file)", 0, str(source) + "\t" + str(message), None, None)


# logger call loop
def __ctrl_async_callback():
    while True:
        src, data = _core.ctrl_pop_log()
        if src is None:
            break
        else:
            skynet.logger.handle(log_record(src, data))


# preinit, register libuv items
def __preinit():
    __msg_watcher.start(__first_msg_callback)
    __ctrl_watcher.start(__ctrl_async_callback)
    p_uv_async_send = libuv_cffi.ffi.addressof(libuv_cffi.lib, "uv_async_send")
    _core.init(libuv_cffi.ffi, p_uv_async_send, __msg_watcher._watcher, __ctrl_watcher._watcher)


__preinit()

def start_with_settings(thread:int, profile:int, settings:Dict[str, Any]):
    global __boot_event
    assert __boot_event is None, "pyskynet.start can only be called once"
    __boot_event = gevent.event.Event()
    setenv("thread", thread)
    setenv("profile", profile)
    # path
    setenv("cservice", ";".join(config.cservice))
    setenv("luaservice", ";".join(config.luaservice))
    setenv("lua_cpath", ";".join(config.lua_cpath))
    setenv("lua_path", ";".join(config.lua_path))
    # scripts
    setenv("lualoader", config.lualoader)
    setenv("bootstrap", config.bootstrap)
    # immutable setting
    setenv("standalone", "1") # used by service_mgr.lua
    setenv("harbor", "0") # used by cdummy
    # custom settings
    setenv("settings", settings)
    _core.start(thread=thread, profile=profile)
    __boot_event.wait()


def main():
    import argparse
    parser = argparse.ArgumentParser(description='pyskynet fast entry')
    parser.add_argument("script", type=str,
                        help="lua service script file", nargs='?', default="")
    parser.add_argument("args", type=str,
                        help="service arguments", nargs='*', default="")
    args = parser.parse_args()
    if args.script != "":
        import pyskynet
        start_with_settings(2, 0, {})
        with open(args.script) as fo:
            script = fo.read()
        pyskynet.foreign.call(__boot_service, "cmdline", args.script, script, *args.args)
    else:
        import pyskynet
        start_with_settings(2, 0, {})
        import pyskynet.foreign
        import code
        import sys
        import readline
        readline.parse_and_bind('tab: complete')

        class PySkynet(code.InteractiveConsole):
            def __init__(self, *args, **kwargs):
                super().__init__()
                sys.ps1 = "(lua)> "

            def runsource(self, *args, **kwargs):
                pyskynet.foreign.call(__boot_service, "repl", args[0])
                return False

            def raw_input(self, *args, **kwargs):
                try:
                    re = super().raw_input(*args, **kwargs)
                    return re
                except KeyboardInterrupt:
                    pass
                print("")
                exit()
        PySkynet().interact()
