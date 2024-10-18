cmake_minimum_required(VERSION 3.16)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(compilers)

project(
    umd_device
    VERSION 0.1.0
    DESCRIPTION "Tenstorrent User Mode Driver"
    HOMEPAGE_URL "https://github.com/tenstorrent/tt-umd"
    LANGUAGES
        CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

CHECK_COMPILERS()

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

add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:RelWithDebInfo>:DEBUG>
)

include(dependencies)

add_subdirectory(${PROJECT_SOURCE_DIR}/device)

option(TT_UMD_BUILD_TESTS "Enables build of tt_umd tests" OFF)
if(TT_UMD_BUILD_TESTS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif(TT_UMD_BUILD_TESTS)
