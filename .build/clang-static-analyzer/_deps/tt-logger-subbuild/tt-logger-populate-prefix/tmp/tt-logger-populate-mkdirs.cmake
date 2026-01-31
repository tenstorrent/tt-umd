# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/workspace/tt_metal/third_party/umd/.cpmcache/tt-logger/87c1a5f2e9d2dd011200eb49c86426c26dec719e")
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.cpmcache/tt-logger/87c1a5f2e9d2dd011200eb49c86426c26dec719e")
endif()
file(MAKE_DIRECTORY
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-build"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix/tmp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix/src/tt-logger-populate-stamp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix/src"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix/src/tt-logger-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix/src/tt-logger-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/tt-logger-subbuild/tt-logger-populate-prefix/src/tt-logger-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
