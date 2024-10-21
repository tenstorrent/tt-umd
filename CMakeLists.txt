cmake_minimum_required(VERSION 3.16)

# Temporarily default to clang-17
if(NOT CMAKE_TOOLCHAIN_FILE)
    message(STATUS "No toolchain file provided. Using default toolchain.")
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_SOURCE_DIR}/cmake/x86-linux-clang-17-toolchain.cmake"
        CACHE STRING
        "Default toolchain file"
        FORCE
    )
endif()

project(
    umd
    VERSION 0.1.0
    DESCRIPTION "Tenstorrent User Mode Driver"
    HOMEPAGE_URL "https://github.com/tenstorrent/tt-umd"
    LANGUAGES
        CXX
)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(check_libcpp)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # This also impacts dependencies brought in through CPM

set(MASTER_PROJECT OFF)
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MASTER_PROJECT ON)
endif()

if(MASTER_PROJECT)
    message(STATUS "UMD: Building as master project")
    if(NOT CMAKE_BUILD_TYPE)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set(CMAKE_BUILD_TYPE
            "Release"
            CACHE STRING
            "Release build is the default"
            FORCE
        )
    endif()

    include(sanitizer_options)
endif()
message(STATUS "UMD build type: ${CMAKE_BUILD_TYPE}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DDEBUG=DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG=DEBUG")

include(${PROJECT_SOURCE_DIR}/cmake/dependencies.cmake)

add_library(umd_common_directories INTERFACE)
target_include_directories(umd_common_directories INTERFACE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/device)

add_subdirectory(${PROJECT_SOURCE_DIR}/device)

option(TT_UMD_BUILD_TESTS "Enables build of tt_umd tests" OFF)
if(TT_UMD_BUILD_TESTS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif(TT_UMD_BUILD_TESTS)

