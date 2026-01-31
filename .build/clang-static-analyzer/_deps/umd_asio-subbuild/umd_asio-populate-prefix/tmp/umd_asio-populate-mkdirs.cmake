# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/workspace/tt_metal/third_party/umd/.cpmcache/umd_asio/83a029aeca9a00050695989a093d0cc1fcd67ba8")
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.cpmcache/umd_asio/83a029aeca9a00050695989a093d0cc1fcd67ba8")
endif()
file(MAKE_DIRECTORY
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-build"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix/tmp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix/src/umd_asio-populate-stamp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix/src"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix/src/umd_asio-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix/src/umd_asio-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/umd_asio-subbuild/umd_asio-populate-prefix/src/umd_asio-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
