# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/workspace/tt_metal/third_party/umd/.cpmcache/yaml-cpp/652ccf99aa11ad40631a0e2faf489e7e553b7858")
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.cpmcache/yaml-cpp/652ccf99aa11ad40631a0e2faf489e7e553b7858")
endif()
file(MAKE_DIRECTORY
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-build"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix/tmp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix/src/yaml-cpp-populate-stamp"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix/src"
  "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix/src/yaml-cpp-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix/src/yaml-cpp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/yaml-cpp-subbuild/yaml-cpp-populate-prefix/src/yaml-cpp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
