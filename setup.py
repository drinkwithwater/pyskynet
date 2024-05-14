
from distutils.command.build_ext import build_ext
from distutils.command.build import build
from setup_ext import *

def create_cython_extensions():
    ext_main = Extension('pyskynet._core',
        include_dirs=INCLUDE_DIRS,
        sources=['src/cy_src/_core.pyx'] +
                list_path(SKYNET_SRC_PATH, ".c", ["skynet_main.c", "skynet_start.c", "skynet_env.c", "skynet_server.c", "skynet_error.c"]) +
                list_path("src/skynet_modify", ".c") +
                list_path("numsky/src/skynet_foreign/", ".c") +
                list_path(LUA_PATH, ".c", ["lua.c", "luac.c"]),
        depends=['src/cy_src/skynet_modify.pxd'],
        define_macros=MACROS,
        libraries=LIBRARIES,
        extra_objects=[])

    ext_seri = Extension('pyskynet._foreign_seri',
        include_dirs=INCLUDE_DIRS,
        sources=['src/cy_src/_foreign_seri.pyx'] +
                list_path('numsky/src/foreign_seri/', '.c', ["lua-foreign_seri.c"]),
        depends=['src/cy_src/skynet_modify.pxd'],
        define_macros=MACROS,
        libraries=LIBRARIES)

    return [ext_main, ext_seri]


class build_with_numpy_cython(build):
    def finalize_options(self):
        super().finalize_options()
        self.distribution.ext_modules = create_skynet_extensions() + create_cython_extensions() + create_lua_extensions() + create_3rd_extensions()
        import numpy
        for extension in self.distribution.ext_modules:
            np_inc = numpy.get_include()
            if not (np_inc in extension.include_dirs):
                extension.include_dirs.append(np_inc)
        from Cython.Build import cythonize
        self.distribution.ext_modules = cythonize(self.distribution.ext_modules, language_level=3)


class build_ext_rename(build_ext):
    def get_export_symbols(self, ext_name):
        # TODO symbol PyInit_xxx will be add in this step, but lua library don't has this symbol, so override this in some platform
        return super().get_export_symbols(ext_name)

    def get_ext_filename(self, ext_name):
        ext_name_last = ext_name.split(".")[-1]
        # cython library start with skynet_py
        if ext_name_last.find("_core") == 0 or ext_name_last.find("_foreign_seri") == 0:
            # for cython library
            return super().get_ext_filename(ext_name)
        else:
            # for lua library
            ext_path = ext_name.split('.')
            return os.path.join(*ext_path) + ".so"

def get_version():
    with open("pyskynet/__init__.py") as fo:
        data = fo.read()
        result = re.search(r'__version__\s*=\s*[\'"]([^\'"]*)[\'"]', data)
        return result.group(1)

def main():
    setup(
            name="pyskynet",
            version=get_version(),
            author="cz",
            author_email="drinkwithwater@gmail.com",
            license='MIT',
            description="PySkynet is a library for using skynet in python.",
            ext_modules=[], # setted in build_with_numpy_cython
            cmdclass={"build_ext": build_ext_rename, "build": build_with_numpy_cython},
            packages=["pyskynet", "skynet", "numsky"],
            package_data={
                "pyskynet": ["service/*",
                            "lualib/*",
                            "thlua_loader.lua",
                            "lualib/*/*",
                            ],
                "skynet": ["service/*",
                          "cservice/*",
                          "luaclib/*",
                          "lualib/*",
                          "lualib/*/*",
                          "lualib/*/*/*",
                          # lua header
                          "3rd/lua/lua.hpp",
                          "3rd/lua/lua.h",
                          "3rd/lua/lauxlib.h",
                          "3rd/lua/lualib.h",
                          "3rd/lua/luaconf.h",
                          # skynet header
                          "skynet-src/spinlock.h",
                          ],
                "numsky": [
                    "src/lua-binding.h",
                    "src/skynet_foreign/*.h",
                ]
            },
            zip_safe=False,
            entry_points={
                "console_scripts": [
                    "pyskynet=pyskynet.boot:main",
                    "pyskynetc=pyskynet.compile:entry",
                ]
            },
            install_requires=[
                "cffi ~= 1.14.2",
                "gevent >= 20.6.0",
                "numpy",
            ],
            url='https://github.com/drinkwithwater/pyskynet',
            setup_requires=["cython", "numpy"],
            python_requires='>=3.5',
        )

main()
