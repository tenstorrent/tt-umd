
set(UMD_DEVICE_SRCS
    blackhole_tt_device.cpp
    grayskull_tt_device.cpp
    chip.cpp
    cluster_descriptor.cpp
    cluster.cpp
    cpuset_lib.cpp
    local_chip.cpp
    pci_device.cpp
    remote_chip.cpp
    soc_descriptor.cpp
    system_util.cpp
    tlb.cpp
    tt_device.cpp
    wormhole_tt_device.cpp
    simulation/tt_simulation_device.cpp
    simulation/tt_simulation_host.cpp
)
add_library(umd_device SHARED ${UMD_DEVICE_SRCS})
target_link_libraries(umd_device 
    PUBLIC yaml-cpp::yaml-cpp umd_common_directories nng uv compiler_flags
    PRIVATE hwloc rt Boost::interprocess
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
