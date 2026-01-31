# Install script for directory: /workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/libfmtd.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/fmt" TYPE FILE FILES
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/args.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/base.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/chrono.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/color.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/compile.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/core.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/format.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/format-inl.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/os.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/ostream.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/printf.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/ranges.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/std.h"
    "/workspace/tt_metal/third_party/umd/.cpmcache/fmt/69912fb6b71fcb1f7e5deca191a2bb4748c4e7b6/include/fmt/xchar.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt" TYPE FILE FILES
    "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/fmt-config.cmake"
    "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/fmt-config-version.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt/fmt-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt/fmt-targets.cmake"
         "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/CMakeFiles/Export/b834597d9b1628ff12ae4314c3a2e4b8/fmt-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt/fmt-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt/fmt-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt" TYPE FILE FILES "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/CMakeFiles/Export/b834597d9b1628ff12ae4314c3a2e4b8/fmt-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fmt" TYPE FILE FILES "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/CMakeFiles/Export/b834597d9b1628ff12ae4314c3a2e4b8/fmt-targets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fmt-core" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/fmt.pc")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/workspace/tt_metal/third_party/umd/.build/clang-static-analyzer/_deps/fmt-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
