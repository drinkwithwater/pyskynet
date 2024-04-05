
import typing
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

def compile(output_file:str, sources:typing.List[str], include_dirs=[]):
    from distutils.command.build_ext import build_ext
    from distutils.core import Distribution, Extension
    class build_ext_purec(build_ext):
        def get_export_symbols(self, ext_name):
            return []
        def get_ext_filename(self, ext_name):
            return output_file
    extension = Extension(output_file.replace(".", "_"),
        include_dirs=include_dirs + get_includes(),
        sources=sources,
        define_macros=get_macros(),
        libraries=[]) # TODO, libraries in args
    dist = Distribution(dict(ext_modules=[extension], cmdclass={'build_ext': build_ext_purec}))
    dist.command_options["build_ext"] = {"inplace": ('command line', 1)}
    dist.commands = ["build_ext"]
    dist.run_commands()

def entry():
    import argparse
    parser = argparse.ArgumentParser(description="pyskynetc, tool to compile lua library ")
    parser.add_argument("sources", type=str, nargs="+",
                        help="c/c++ sources")
    parser.add_argument("-o", dest="output_file", type=str, required=True,
                        help="output file for library")
    parser.add_argument("-I", dest="include_dirs", action="append", default=[],
                        help="c/c++ header directories")
    args = parser.parse_args()
    compile(args.output_file, args.sources, args.include_dirs)
