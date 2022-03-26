#!/usr/bin/env python

import os
import sys

from distutils.core import setup
from distutils import spawn
import distutils.command.build as _build
from distutils.sysconfig import get_python_lib
import setuptools

def extend_build(package_name):
    class build(_build.build):
        description = "Installs the foo."
        user_options = [
            ('installprefix=', None, 'Specify the foo to bar.'),
        ]
        def initialize_options(self):
            self.installprefix = None
        def finalize_options(self):
            pass
        
        def run(self):
            cwd = os.getcwd()
            if spawn.find_executable('cmake') is None:
                sys.stderr.write("CMake is required to build this package.\n")
                sys.exit(-1)
            _source_dir = os.path.split(__file__)[0]
            _build_dir = os.path.join(_source_dir, 'build_setup_py')
            if self.installprefix is None:
                _prefix = os.path.join(_source_dir, package_name)
            else:
                _prefix = self.installprefix
            try:
                spawn.spawn(['cmake',
                             '-H{0}'.format(_source_dir),
                             '-B{0}'.format(_build_dir),
                             '-DCMAKE_INSTALL_PREFIX={0}'.format(_prefix),
                             '-DCMAKE_BUILD_TYPE=Release'
                             ])
                spawn.spawn(['cmake',
                             '--build', _build_dir,
                             '--target', 'install'])
                os.chdir(cwd)
            except spawn.DistutilsExecError:
                sys.stderr.write("Error while building with CMake\n")
                sys.exit(-1)
            #_build.build.run(self)
    return build

with open("README.md", "r", encoding='utf-8') as fh:
    long_description = fh.read()
setuptools.setup(
      name='pystm32flash',
      version='0.0.3',
      description='Library using stm32flash in python.',
      long_description=long_description,
      long_description_content_type = "text/markdown",      
      author='jiangshan00001',
      author_email='710806594@qq.com',
      url='https://github.com/Jiangshan00001/pystm32flash',
      packages=setuptools.find_packages(),
      license='MIT',
      install_requires=['cffi'],
      cmdclass={'mybuild': extend_build('pystm32flash')},
      classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ]
      )
      