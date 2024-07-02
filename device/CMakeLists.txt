add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/simulation)

set(UMD_DEVICE_SRCS
    architecture_implementation.cpp
    blackhole_implementation.cpp
    cpuset_lib.cpp
    grayskull_implementation.cpp
    tlb.cpp
    tt_cluster_descriptor.cpp
    tt_device.cpp
    tt_emulation_stub.cpp
    tt_silicon_driver.cpp
    tt_silicon_driver_common.cpp
    tt_soc_descriptor.cpp
    tt_versim_stub.cpp
    wormhole_implementation.cpp
)
add_library(umd_device SHARED ${UMD_DEVICE_SRCS})
target_link_libraries(umd_device 
    PUBLIC umd_common_directories simulation
    PRIVATE yaml-cpp hwloc rt fmt::fmt-header-only
)
set_target_properties(umd_device PROPERTIES 
    OUTPUT_NAME device
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE ON
)
