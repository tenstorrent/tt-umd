include(GNUInstallDirs)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(UMD_DEVICE_SRCS
    architecture_implementation.cpp
    cpuset_lib.cpp
    tlb.cpp
    tt_cluster_descriptor.cpp
    tt_device.cpp
    tt_silicon_driver.cpp
    tt_silicon_driver_common.cpp
    tt_soc_descriptor.cpp
    simulation/tt_simulation_device.cpp
    simulation/tt_simulation_host.cpp
    blackhole/blackhole_implementation.cpp
    grayskull/grayskull_implementation.cpp
    wormhole/wormhole_implementation.cpp
    pcie/pci_device.cpp
)

add_library(umd_device SHARED ${UMD_DEVICE_SRCS})

target_include_directories(
    umd_device
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/umd>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/umd/device>
)

# FlatBuffers is public - exposed to tt_metal by tt_simulation_device_generated.h
# nng is public - exposed to tt_metal by tt_simulation_host.hpp
target_link_libraries(
    umd_device
    PUBLIC
        nng
        FlatBuffers::FlatBuffers
    PRIVATE
        hwloc
        rt
        Boost::interprocess
        fmt::fmt-header-only
        uv
        yaml-cpp::yaml-cpp
)

set_target_properties(
    umd_device
    PROPERTIES
        OUTPUT_NAME
            device
        LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/lib
)
