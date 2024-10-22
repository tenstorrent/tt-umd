
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
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/device>
)
target_link_libraries(umd_device 
    PUBLIC yaml-cpp::yaml-cpp nng uv compiler_flags
    PRIVATE hwloc rt Boost::interprocess fmt::fmt-header-only
)
target_include_directories(umd_device PUBLIC
    ${flatbuffers_SOURCE_DIR}/include
    ${nanomsg_SOURCE_DIR}/include
    ${libuv_SOURCE_DIR}/include
)
set_target_properties(umd_device PROPERTIES 
    OUTPUT_NAME device
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE ON
)
