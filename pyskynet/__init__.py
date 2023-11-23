
###############################
# some api different from lua #
###############################
import inspect
import pyskynet.boot
import pyskynet.skynet_py_mq
import pyskynet.foreign as foreign
import pyskynet.skynet_py_main as skynet_py_main
import pyskynet.skynet as skynet

__version__ = '0.2.3'
start = pyskynet.boot.start
join = pyskynet.boot.join

proto = skynet # for compatiable with old code

#################
# env set & get #
#################


def getenv(key):
    data = skynet_py_main.getlenv(key)
    if data is None:
        return None
    else:
        return foreign.remoteunpack(data)[0]


def setenv(key, value):
    if skynet_py_main.self() != 0:
        assert (key is None) or (getenv(key) is None), "Can't setenv exist key : %s " % key
    msg_ptr, msg_size = foreign.remotepack(value)
    skynet_py_main.setlenv(key, msg_ptr, msg_size)
    foreign.trash(msg_ptr, msg_size)


def envs():
    key = None
    re = {}
    while True:
        key = skynet_py_main.nextenv(key)
        if(key is None):
            break
        else:
            re[key] = getenv(key)
    return re


###############
# service api #
###############
def newservice(service_name, *args):
    assert type(service_name) == str or type(service_name) == bytes, "newservice's name must be str or bytes"
    for arg in args:
        assert type(arg) == str or type(arg) == bytes, "newservice's arg must be str or bytes"
    return skynet.call(".launcher", skynet.PTYPE_LUA, "LAUNCH", "snlua", service_name, *args)[0]


def uniqueservice(service_name, *args):
    assert type(service_name) == str or type(service_name) == bytes, "uniqueservice's name must be str or bytes"
    for arg in args:
        assert type(arg) == str or type(arg) == bytes, "uniqueservice's arg must be str or bytes"
    return skynet.call(".service", skynet.PTYPE_LUA, "LAUNCH", service_name, *args)[0]


def scriptservice(scriptCode, *args):
    scriptIndex = skynet_py_main.refscript(scriptCode)
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
    address = skynet_py_main.self()
    assert address > 0, "service pyholder not start "
    return address

