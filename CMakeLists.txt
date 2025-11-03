cmake_minimum_required(VERSION 3.16)

option(TT_UMD_BUILD_JTAG "Enable JTAG support" OFF)
set(DOWNLOAD_TTEXALENS_PRIVATE ${TT_UMD_BUILD_JTAG})

include(cmake/compilers.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # This also impacts dependencies brought in through CPM
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(DEFINED ENV{CMAKE_C_COMPILER} AND DEFINED ENV{CMAKE_CXX_COMPILER})
    message(STATUS "Setting C and C++ compiler from environment variables")
    set(CMAKE_C_COMPILER $ENV{CMAKE_C_COMPILER})
    set(CMAKE_CXX_COMPILER $ENV{CMAKE_CXX_COMPILER})
endif()

if(CMAKE_CXX_COMPILER AND CMAKE_C_COMPILER)
    message(STATUS "Using specifed C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Using specifed C compiler: ${CMAKE_C_COMPILER}")
else()
    message(STATUS "No C or C++ compiler specified, defaulting to Clang-17")
    FIND_AND_SET_CLANG17()
endif()

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TT_UMD_VERSION)
string(STRIP "${TT_UMD_VERSION}" TT_UMD_VERSION)

project(
    umd
    VERSION ${TT_UMD_VERSION}
    DESCRIPTION "Tenstorrent User Mode Driver"
    HOMEPAGE_URL "https://github.com/tenstorrent/tt-umd"
    LANGUAGES
        C
        CXX
)

message(STATUS "CMake version: ${CMAKE_VERSION}")

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
CHECK_COMPILERS()
include(check_libcpp)

include(GNUInstallDirs)
set(MASTER_PROJECT OFF)
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MASTER_PROJECT ON)
endif()

if(MASTER_PROJECT)
    message(STATUS "UMD: Building as master project")
    if(NOT CMAKE_BUILD_TYPE)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Release build is the default" FORCE)
    endif()

    include(sanitizers)
endif()

message(STATUS "UMD build type: ${CMAKE_BUILD_TYPE}")

option(TT_UMD_BUILD_TESTS "Enables build of tt_umd tests" OFF)
option(TT_UMD_BUILD_SIMULATION "Enables build of tt_umd simulation harnessing" OFF)
option(TT_UMD_BUILD_PYTHON "Enables build of tt_umd python bindings" OFF)
option(TT_UMD_BUILD_TOOLS "Enables build of tt_umd tools" ON)
option(TT_UMD_BUILD_EXAMPLES "Enables build of tt_umd examples" OFF)
option(TT_UMD_BUILD_ALL "Enables build of all tt_umd components (tests, simulation, python, tools, examples)" OFF)

# If BUILD_ALL is enabled, enable all individual build options
if(TT_UMD_BUILD_ALL)
    set(TT_UMD_BUILD_TESTS ON CACHE BOOL "Enabled by TT_UMD_BUILD_ALL" FORCE)
    set(TT_UMD_BUILD_SIMULATION ON CACHE BOOL "Enabled by TT_UMD_BUILD_ALL" FORCE)
    set(TT_UMD_BUILD_PYTHON ON CACHE BOOL "Enabled by TT_UMD_BUILD_ALL" FORCE)
    set(TT_UMD_BUILD_TOOLS ON CACHE BOOL "Enabled by TT_UMD_BUILD_ALL" FORCE)
    set(TT_UMD_BUILD_EXAMPLES ON CACHE BOOL "Enabled by TT_UMD_BUILD_ALL" FORCE)
    message(STATUS "TT_UMD_BUILD_ALL enabled - building all components")
endif()

# Control compile time logging
if(NOT DEFINED TT_UMD_ENABLE_LOGGING)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_default_enable_logging ON)
    else()
        set(_default_enable_logging OFF)
    endif()
endif()
option(TT_UMD_ENABLE_LOGGING "Enables inclusion of log_trace and log_debug into the binary." ${_default_enable_logging})
if(TT_UMD_ENABLE_LOGGING)
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
endif()

add_subdirectory(third_party)

# Enable all warnings and treat them as errors
# Ignore unused variables and functions since there is no harm.
# TODO: Re-enable sign-compare
add_compile_options(
    "$<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:-march=x86-64-v3>"
    -Wall
    -Werror
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-sign-compare
)

# Conditionally add -Wno-maybe-uninitialized for GCC
# If this is left enabled it produces a hard to track down issue in stimulus_generators.hpp with ConstrainedTemplateTemplateGenerator
# TODO: Fix the issue and re-enable this warning
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
    add_compile_options(-Wno-nonnull)
endif()

add_subdirectory(common)
if(TT_UMD_BUILD_SIMULATION)
    message(STATUS "Building ${PROJECT_NAME} with Simulation")
endif()
add_subdirectory(device)
if(TT_UMD_BUILD_PYTHON)
    message(STATUS "Building ${PROJECT_NAME} with Python Bindings")
    add_subdirectory(nanobind)
endif()
add_subdirectory(src)
if(TT_UMD_BUILD_TOOLS)
    message(STATUS "Building ${PROJECT_NAME} with Tools")
    add_subdirectory(tools)
endif()

if(TT_UMD_BUILD_EXAMPLES)
    message(STATUS "Building ${PROJECT_NAME} with Examples")
    add_subdirectory(examples)
endif()

if(TT_UMD_BUILD_TESTS)
    message(STATUS "Building ${PROJECT_NAME} with Tests")
    add_subdirectory(tests)
endif(TT_UMD_BUILD_TESTS)

include(packaging)
