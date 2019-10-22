import os
import re
import sys
import platform
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build the following extensions: " +
                               ", ".join(e.name for e in self.extensions))

        if platform.system() == "Windows":
            cmake_version = LooseVersion(re.search(r'version\s*([\d.]+)', out.decode()).group(1))
            if cmake_version < '3.1.0':
                raise RuntimeError("CMake >= 3.1.0 is required on Windows")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + extdir,
                      '-DPYTHON_EXECUTABLE=' + sys.executable]
        print(extdir)
        subprocess.check_call(['cmake', '.'] + cmake_args, cwd=ext.sourcedir)
        subprocess.check_call(['cmake', '--build', '.'], cwd=ext.sourcedir)

setup(
    name='mlir_bindings',
    version='0.0.1',
    author='Michal Fojtak',
    author_email='mfojtak@seznam.cz',
    description='mlir python bindings',
    long_description='',
    ext_modules=[CMakeExtension('mlir_bindings', sourcedir=".")],
    cmdclass=dict(build_ext=CMakeBuild),
    zip_safe=False,
)