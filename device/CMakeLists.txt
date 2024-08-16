
set(UMD_DEVICE_SRCS
    architecture_implementation.cpp
    blackhole_implementation.cpp
    cpuset_lib.cpp
    grayskull_implementation.cpp
    tlb.cpp
    tt_cluster_descriptor.cpp
    tt_device.cpp
    tt_silicon_driver.cpp
    tt_silicon_driver_common.cpp
    tt_soc_descriptor.cpp
    wormhole_implementation.cpp
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
