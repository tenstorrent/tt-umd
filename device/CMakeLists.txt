
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
target_link_libraries(
    umd_device
    PUBLIC
        nng
        uv
	fmt::fmt-header-only
	FlatBuffers::FlatBuffers
    PRIVATE
        hwloc
        rt
        Boost::interprocess
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
#install(TARGETS umd_device
#    EXPORT umdTargets
#    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    COMPONENT dev
#)
#install(EXPORT umdTargets
#    FILE umdTargets.cmake
#    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/umd
#)
