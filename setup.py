# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import os
import subprocess
import sys
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

class CMakeBuildExt(build_ext):
    def build_extension(self, ext):
        # Get the directory where the extension should be built
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            # f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DTT_UMD_BUILD_TESTS=OFF",
            f"-DTT_UMD_BUILD_SIMULATION=OFF",
            f"-DTT_UMD_BUILD_PYTHON=ON",
            f"-DCMAKE_BUILD_TYPE={'Release'}",  # Default to Release build type
        ]

        # Run CMake to configure and build the extension
        build_temp = os.path.abspath(self.build_temp)
        os.makedirs(build_temp, exist_ok=True)

        # Configure step
        subprocess.check_call(
            ["cmake", "-B", build_temp, "-G", "Ninja"] + cmake_args, cwd=ext.sourcedir
        )

        # Build step
        subprocess.check_call(
            ["cmake", "--build", build_temp, "--target", "device", "nanobind_tt_umd"], cwd=ext.sourcedir
        )

class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

setup(
    name="tt_umd",
    version="1.0.0",
    author="Tenstorrent",
    url="http://www.tenstorrent.com",
    author_email="info@tenstorrent.com",
    description="User Mode Driver for tenstorrent",
    ext_modules=[CMakeExtension("tt_umd", sourcedir=".")],
    # packages=["tt_umd"],  # Create a folder for your package
    # package_data={
    #     "tt_umd": ["tt_umd.so", "tt_umd.cpython-310-x86_64-linux-gnu.so"],
    # },
    cmdclass={"build_ext": CMakeBuildExt},
    zip_safe=False,
)
