cmake_minimum_required(VERSION 3.16)

include(cmake/compilers.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (DEFINED ENV{CMAKE_C_COMPILER} AND DEFINED ENV{CMAKE_CXX_COMPILER})
    message(STATUS "Setting C and C++ compiler from environment variables")
    set(CMAKE_C_COMPILER $ENV{CMAKE_C_COMPILER})
    set(CMAKE_CXX_COMPILER $ENV{CMAKE_CXX_COMPILER})
endif()

if (CMAKE_CXX_COMPILER AND CMAKE_C_COMPILER)
    message(STATUS "Using specifed C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Using specifed C compiler: ${CMAKE_C_COMPILER}")
else()
    message(STATUS "No C or C++ compiler specified, defaulting to Clang-17")
    FIND_AND_SET_CLANG17()
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
        set(CMAKE_BUILD_TYPE
            "Release"
            CACHE STRING
            "Release build is the default"
            FORCE
        )
    endif()

    option(ENABLE_ASAN "Enable build with AddressSanitizer" OFF)
    message(STATUS "Build with ASAN: ${ENABLE_ASAN}")

    set(SANITIZER_ENABLED ${ENABLE_ASAN})

    option(ENABLE_MSAN "Enable build with MemorySanitizer" OFF)
    message(STATUS "Build with MSAN: ${ENABLE_MSAN}")

    if(SANITIZER_ENABLED AND ENABLE_MSAN)
        message(FATAL_ERROR "Multiple sanitizers are not supported")
    elseif(ENABLE_MSAN)
        set(SANITIZER_ENABLED ${ENABLE_MSAN})
    endif()

    option(ENABLE_TSAN "Enable build with ThreadSanitizer" OFF)
    message(STATUS "Build with TSAN: ${ENABLE_TSAN}")

    if(SANITIZER_ENABLED AND ENABLE_TSAN)
        message(FATAL_ERROR "Multiple sanitizers are not supported")
    elseif(ENABLE_TSAN)
        set(SANITIZER_ENABLED ${ENABLE_TSAN})
    endif()

    option(ENABLE_UBSAN "Enable build with UndefinedBehaviorSanitizer" OFF)
    message(STATUS "Build with UBSAN: ${ENABLE_UBSAN}")

    if(SANITIZER_ENABLED AND ENABLE_UBSAN)
        message(FATAL_ERROR "Multiple sanitizers are not supported")
    endif()

    unset(SANITIZER_ENABLED)

    add_library(compiler_flags INTERFACE)
    target_compile_options(
        compiler_flags
        INTERFACE -DFMT_HEADER_ONLY
                $<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>
                $<$<BOOL:${ENABLE_MSAN}>:-fsanitize=memory>
                $<$<BOOL:${ENABLE_TSAN}>:-fsanitize=thread>
                $<$<BOOL:${ENABLE_UBSAN}>:-fsanitize=undefined>)

    add_library(linker_flags INTERFACE)
    target_link_options(
        linker_flags
        INTERFACE
        $<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>
        $<$<BOOL:${ENABLE_MSAN}>:-fsanitize=memory>
        $<$<BOOL:${ENABLE_TSAN}>:-fsanitize=thread>
        $<$<BOOL:${ENABLE_UBSAN}>:-fsanitize=undefined>)

    target_link_libraries(compiler_flags INTERFACE linker_flags)
endif()
message(STATUS "UMD build type: ${CMAKE_BUILD_TYPE}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DDEBUG=DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG=DEBUG")

include(dependencies)

add_subdirectory(device)

option(TT_UMD_BUILD_TESTS "Enables build of tt_umd tests" OFF)
if(TT_UMD_BUILD_TESTS)
    add_subdirectory(tests)
endif(TT_UMD_BUILD_TESTS)

include(packaging)
