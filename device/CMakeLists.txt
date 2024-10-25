
set(POSITION_INDEPENDENT_CODE ON)

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

add_library(device SHARED ${UMD_DEVICE_SRCS})
add_library(${PROJECT_NAME}::device ALIAS device)
add_library(${PROJECT_NAME}_device ALIAS device) # For legacy I guess

target_include_directories(
    device
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/device>
)

# flatbuffers is public - exposed to tt_metal by tt_simulation_device_generated.h
# nng is public - exposed to tt_metal by tt_simulation_host.hpp
target_link_libraries(
    device
    PUBLIC
        nng
        flatbuffers
        uv
    PRIVATE
        hwloc
        rt
        Boost::interprocess
        fmt::fmt-header-only
        yaml-cpp::yaml-cpp
)

install(
    TARGETS
        device
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT dev
)

install(
    EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    NAMESPACE ${PROJECT_NAME}::
)

# Add a custom command to copy the library to build/lib
add_custom_command(
    TARGET device
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:device> ${CMAKE_BINARY_DIR}/lib/
    COMMENT "Copying device library to build/lib"
)

# No separation of public and private header files
# I can only assume everything is public
install(
    DIRECTORY
        ${PROJECT_SOURCE_DIR}/device
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/device
    FILES_MATCHING
    PATTERN
    "*.h"
    PATTERN
    "*.hpp"
)
install(
    DIRECTORY
        ${PROJECT_SOURCE_DIR}/common
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/common
    FILES_MATCHING
    PATTERN
    "*.h"
    PATTERN
    "*.hpp"
)
