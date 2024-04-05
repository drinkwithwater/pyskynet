
import os
import sys

def get_includes():
    return [
        os.path.abspath(os.path.join(os.path.dirname(__file__), src_path)) for src_path in [
            "../skynet/skynet-src",
            "../skynet/3rd/lua",
            "../numsky/src",
        ]
    ]

def get_macros():
    MACROS = [("NOUSE_JEMALLOC", None), ("BUILD_FOR_PYSKYNET", None), ("__STDC_NO_ATOMICS__", None)]
    if sys.platform == "linux":
        MACROS += [("LUA_USE_LINUX", None)]
    elif sys.platform == "darwin":
        MACROS += [("LUA_USE_MACOSX", None)]
    return MACROS
