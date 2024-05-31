cmake_minimum_required(VERSION 3.10)
project(umd_device)
find_package(yaml-cpp REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(MASTER_PROJECT OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MASTER_PROJECT ON)
    message("-- UMD: Building as master project")
endif()

if(NOT DEFINED ENV{ARCH_NAME})
    message(FATAL_ERROR "Please set ARCH_NAME to grayskull, wormhole_b0, or blackhole")
elseif($ENV{ARCH_NAME} STREQUAL "grayskull")
    message("-- UMD: Building for Grayskull")
    include_directories(device/grayskull)
    include_directories(src/firmware/riscv/grayskull)
elseif($ENV{ARCH_NAME} STREQUAL "wormhole_b0")
    message("-- UMD: Building for Wormhole")
    include_directories(device/wormhole)
    include_directories(src/firmware/riscv/wormhole)
elseif($ENV{ARCH_NAME} STREQUAL "blackhole")
    message("-- UMD: Building for Blackhole")
    include_directories(device/blackhole)
    include_directories(src/firmware/riscv/blackhole)
endif()

add_subdirectory(third_party/fmt)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(umd_device_objects OBJECT
    device/architecture_implementation.cpp
    device/blackhole_implementation.cpp
    device/cpuset_lib.cpp
    device/grayskull_implementation.cpp
    device/tlb.cpp
    device/tt_cluster_descriptor.cpp
    device/tt_device.cpp
    device/tt_emulation_stub.cpp
    device/tt_silicon_driver.cpp
    device/tt_silicon_driver_common.cpp
    device/tt_soc_descriptor.cpp
    device/tt_versim_stub.cpp
    device/wormhole_implementation.cpp
)
set_target_properties(umd_device_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(umd_device_objects PRIVATE fmt::fmt-header-only)
add_library(umd_device SHARED $<TARGET_OBJECTS:umd_device_objects>)
target_link_libraries(umd_device PRIVATE yaml-cpp hwloc rt)
set_target_properties(umd_device PROPERTIES OUTPUT_NAME device)
set_target_properties(umd_device PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
