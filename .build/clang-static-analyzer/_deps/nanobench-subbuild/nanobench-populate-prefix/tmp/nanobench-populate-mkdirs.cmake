# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/workspace/tt_metal/third_party/umd/.cpmcache/nanobench/5ec5aaf271dc24012a47a909a16df511c81590f9")
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.cpmcache/nanobench/5ec5aaf271dc24012a47a909a16df511c81590f9")
endif()
file(MAKE_DIRECTORY
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-build"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix/tmp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix/src/nanobench-populate-stamp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix/src"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix/src/nanobench-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix/src/nanobench-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/nanobench-subbuild/nanobench-populate-prefix/src/nanobench-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
