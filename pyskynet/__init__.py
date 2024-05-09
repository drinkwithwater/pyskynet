
###############################
# some api different from lua #
###############################
from pyskynet.boot import config, start_with_settings, gevent
import pyskynet._core
import pyskynet.foreign as foreign
import pyskynet._core as _core
import pyskynet.skynet as skynet
import inspect
from typing import Dict, Any

__all__ = [
    "__version__",

    "skynet",
    "foreign",

    "config",

    "newservice",
    "uniqueservice",
    "scriptservice",

    "self",

    "start",
    "exit",
    "join",

    "settings",
]

__version__ = '0.3.1'
settings:Dict[str, Any] = {}

def start():
    start_with_settings(settings)

###############
# service api #
###############
def newservice(service_name, *args):
    assert type(service_name) == str or type(service_name) == bytes, "newservice's name must be str or bytes"
    for arg in args:
        assert type(arg) == str or type(arg) == bytes, "newservice's arg must be str or bytes"
    ret = skynet.call(".launcher", skynet.PTYPE_LUA, "LAUNCH", "snlua", service_name, *args)
    if len(ret) > 0:
        return ret[0]
    else:
        return None


def uniqueservice(service_name, *args):
    assert type(service_name) == str or type(service_name) == bytes, "uniqueservice's name must be str or bytes"
    for arg in args:
        assert type(arg) == str or type(arg) == bytes, "uniqueservice's arg must be str or bytes"
    return skynet.call(".service", skynet.PTYPE_LUA, "LAUNCH", service_name, *args)[0]


def scriptservice(scriptCode, *args):
    scriptIndex = _core.refscript(scriptCode)
    frameinfo = inspect.getframeinfo(inspect.currentframe().f_back)
    scriptName = frameinfo.filename + ":" + str(frameinfo.lineno)
    return newservice("script_service", scriptName, str(scriptIndex), *args)


#class __CanvasService(object):
#    def __init__(self, service):
#        self.service = service
#
#    def reset(self, *args):
#        return foreign.call(self.service, "reset", *args)
#
#    def render(self, *args):
#        return foreign.call(self.service, "render", *args)
#
#    def __del__(self):
#        return foreign.send(self.service, "exit")


#def canvas(script, name="unknowxml"):
#    canvas_service = newservice("canvas_service")
#    foreign.call(canvas_service, "init", script, name)
#    return __CanvasService(canvas_service)


def self():
    address = _core.self()
    assert address > 0, "service pyholder not start "
    return address

__python_exit = exit


def exit():
    __python_exit()

__join_event = gevent.event.Event()

def join():
    gevent.signal_handler(gevent.signal.SIGINT, __join_event.set)
    __join_event.wait()
