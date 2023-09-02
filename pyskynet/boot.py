
# 1. import gevent, gevent must use libuv
import gevent
gevent.config.loop = "libuv"
import os
import sys
import gevent.libuv._corecffi as libuv_cffi
import gevent.event

# 2. dlopen with flags RTLD_GLOBAL
flags = sys.getdlopenflags()
sys.setdlopenflags(flags | os.RTLD_GLOBAL)
import pyskynet.skynet_py_main as skynet_py_main
sys.setdlopenflags(flags)

# 3. some module
import pyskynet.skynet_py_foreign_seri
import pyskynet.skynet_py_mq as skynet_py_mq

__boot_event = None
__exit_event = gevent.event.Event()
__watcher = gevent.get_hub().loop.async_()

boot_service = None

__init_funcs = []


# first callback, waiting for skynet_py_boot
def __first_msg_callback():
    global boot_service
    import pyskynet.skynet as skynet
    source, type_id, session, ptr, length = skynet_py_mq.crecv()
    # assert first message ( c.send(".python", 0, 0, "") )
    assert type_id == 0, "first message type must be 0 but get %s" % type_id
    assert session == 0, "first message session must be 0 but get %s" % session
    boot_service, = pyskynet.skynet_py_foreign_seri.luaunpack(ptr, length)
    __watcher.callback = lambda: gevent.spawn(skynet.__async_handle)
    gevent.spawn(skynet.__async_handle)
    __boot_event.set()


# preinit, register libuv items
def __preinit():
    __watcher.start(__first_msg_callback)
    p_uv_async_send = libuv_cffi.ffi.addressof(libuv_cffi.lib, "uv_async_send")
    p_uv_async_t = __watcher._watcher
    skynet_py_main.init(libuv_cffi.ffi, p_uv_async_send, p_uv_async_t)


__preinit()

SKYNET_ROOT = os.path.join(os.path.abspath(
    os.path.dirname(__file__)), "../skynet")
PYSKYNET_ROOT = os.path.abspath(os.path.dirname(__file__))

config = {
    "thread": 8,

    # skynet service path
    "cservice": [SKYNET_ROOT+"/cservice/?.so"],
    "luaservice": [
        SKYNET_ROOT+"/service/?.lua",
        PYSKYNET_ROOT+"/service/?.lua", PYSKYNET_ROOT+"/service/?.thlua",
        "./?.lua", "./?.thlua"
    ],

    # lua require path
    "lua_cpath": [SKYNET_ROOT+"/luaclib/?.so", PYSKYNET_ROOT+"/lualib/?.so", "./?.so"],
    "lua_path": [
        SKYNET_ROOT+"/lualib/?.lua",
        PYSKYNET_ROOT+"/lualib/?.lua", PYSKYNET_ROOT+"/lualib/?.thlua",
        "./?.lua", "./?.thlua"
    ],

    # script
    #"lualoader": SKYNET_ROOT+"/lualib/loader.lua",
    "lualoader": PYSKYNET_ROOT+"/thlua_loader.lua",
    "bootstrap": "snlua skynet_py_boot",
    "logservice": "snlua",
    "logger": "skynet_py_logger",

    # profile
    "profile": 0,

    # immutable setting, don't use this ...
    "standalone": "1",  # used by service_mgr.lua
    "harbor": "0",  # used by cdummy
}

def init(func):
    if __init_funcs is None:
        func()
    else:
        __init_funcs.append(func)


def start():
    global __boot_event
    if not (__boot_event is None):
        __boot_event.wait()
        return
    __boot_event = gevent.event.Event()
    global __init_funcs
    #assert type(path) == list, "start path must be list"
    #for f in path:
    #    for key in ["lua_path", "luaservice"]:
    #        config[key].append(f)
    #assert type(cpath) == list, "start cpath must be list"
    #for f in cpath:
    #    for key in ["lua_cpath"]:
    #        config[key].append(f)
    #config["thread"] = thread
    import pyskynet
    for k, v in config.items():
        if type(v) == list:
            v = ";".join(v)
        pyskynet.setenv(k, v)
    skynet_py_main.start(thread=config["thread"], profile=config["profile"])
    __boot_event.wait()
    funcs = __init_funcs
    __init_funcs = None
    for f in funcs:
        f()


def join():
    __exit_event.wait()


__python_exit = exit


def exit():
    skynet_py_main.exit()
    __exit_event.set()
    __python_exit()


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
        start()
        with open(args.script) as fo:
            script = fo.read()
        pyskynet.foreign.call(boot_service, "cmdline", args.script, script, *args.args)
    else:
        import pyskynet
        start()
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
                pyskynet.foreign.call(boot_service, "repl", args[0])
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
