cmake_minimum_required(VERSION 3.16)
project(umd_device)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(MASTER_PROJECT OFF)
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MASTER_PROJECT ON)
    message("-- UMD: Building as master project")
endif()

if(MASTER_PROJECT)
    if(NOT CMAKE_BUILD_TYPE)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Release build is the default" FORCE)
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
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -fdiagnostics-color=always")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DDEBUG=DEBUG -fdiagnostics-color=always")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG=DEBUG -fdiagnostics-color=always")

include(${PROJECT_SOURCE_DIR}/cmake/dependencies.cmake)

add_library(umd_common_directories INTERFACE)
target_include_directories(umd_common_directories INTERFACE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/new_device ${PROJECT_SOURCE_DIR}/third_party/fmt/include)

if(NOT DEFINED ENV{ARCH_NAME})
    message(FATAL_ERROR "Please set ARCH_NAME to grayskull, wormhole_b0, or blackhole")
elseif($ENV{ARCH_NAME} STREQUAL "grayskull")
    message(STATUS "UMD: Building for Grayskull")
    target_include_directories(umd_common_directories INTERFACE 
        ${PROJECT_SOURCE_DIR}/new_device/grayskull 
        ${PROJECT_SOURCE_DIR}/src/firmware/riscv/grayskull
    )
elseif($ENV{ARCH_NAME} STREQUAL "wormhole_b0")
    message(STATUS "UMD: Building for Wormhole")
    target_include_directories(umd_common_directories INTERFACE 
        ${PROJECT_SOURCE_DIR}/new_device/wormhole 
        ${PROJECT_SOURCE_DIR}/src/firmware/riscv/wormhole
    )
elseif($ENV{ARCH_NAME} STREQUAL "blackhole")
    message(STATUS "UMD: Building for Blackhole")
    target_include_directories(umd_common_directories INTERFACE 
        ${PROJECT_SOURCE_DIR}/new_device/blackhole 
        ${PROJECT_SOURCE_DIR}/src/firmware/riscv/blackhole
    )
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/new_device)
add_subdirectory(${PROJECT_SOURCE_DIR}/tests EXCLUDE_FROM_ALL)

