
import os
from typing import List

__SKYNET_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../skynet"))
__PYSKYNET_ROOT = os.path.abspath(os.path.dirname(__file__))

__all__ = [
        "thread",
        "profile",

        "cservice",
        "luaservice",
        "lua_cpath",
        "lua_path",

        "lualoader",
        "bootstrap",
        "logservice",
        "logger",

        "settings",
]

thread:int = 8

# profile
profile = 0

# skynet service path
cservice:List[str] = [__SKYNET_ROOT+"/cservice/?.so"]

luaservice:List[str] = [
    __SKYNET_ROOT+"/service/?.lua",
    __PYSKYNET_ROOT+"/service/?.lua", __PYSKYNET_ROOT+"/service/?.thlua",
    "./?.lua", "./?.thlua"
]

# lua require path
lua_cpath:List[str] = [
        __SKYNET_ROOT+"/luaclib/?.so",
        __PYSKYNET_ROOT+"/lualib/?.so",
        "./?.so"
]

lua_path:List[str] = [
    __SKYNET_ROOT+"/lualib/?.lua",
    __PYSKYNET_ROOT+"/lualib/?.lua", __PYSKYNET_ROOT+"/lualib/?.thlua",
    "./?.lua", "./?.thlua"
]

# script
lualoader:str = __PYSKYNET_ROOT+"/thlua_loader.lua"
bootstrap:str = "snlua skynet_py_boot"
logservice:str = "snlua"
logger:str = "skynet_py_logger"
