set(POSITION_INDEPENDENT_CODE ON)

set(FBS_FILE ${PROJECT_SOURCE_DIR}/device/simulation/tt_simulation_device.fbs)
get_filename_component(FBS_FILE_NAME ${FBS_FILE} NAME_WLE)
set(FBS_GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${FBS_FILE_NAME}_generated.h")
add_custom_command(
    OUTPUT
        ${FBS_GENERATED_HEADER}
    COMMAND
        flatc --cpp -o "${CMAKE_CURRENT_BINARY_DIR}/" ${FBS_FILE}
    DEPENDS
        flatc
        ${FBS_FILE}
    COMMENT "Generating FlatBuffers header ${FBS_GENERATED_HEADER}"
    VERBATIM
)

add_library(device SHARED)
add_library(${PROJECT_NAME}::device ALIAS device)

target_sources(
    device
    PRIVATE
        architecture_implementation.cpp
        chip/chip.cpp
        chip/local_chip.cpp
        chip/mock_chip.cpp
        chip/remote_chip.cpp
        tt_device/tlb_manager.cpp
        cluster.cpp
        coordinate_manager.cpp
        grayskull/grayskull_implementation.cpp
        wormhole/wormhole_implementation.cpp
        blackhole/blackhole_implementation.cpp
        hugepage.cpp
        pcie/pci_device.cpp
        simulation/tt_simulation_device.cpp
        simulation/tt_simulation_host.cpp
        tlb.cpp
        tt_cluster_descriptor.cpp
        tt_device/blackhole_tt_device.cpp
        tt_device/grayskull_tt_device.cpp
        tt_device/tt_device.cpp
        tt_device/wormhole_tt_device.cpp
        tt_silicon_driver_common.cpp
        tt_soc_descriptor.cpp
        grayskull/grayskull_coordinate_manager.cpp
        wormhole/wormhole_coordinate_manager.cpp
        blackhole/blackhole_coordinate_manager.cpp
        blackhole/blackhole_arc_message_queue.cpp
        xy_pair.cpp
        ${FBS_GENERATED_HEADER}
)

target_include_directories(
    device
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/device>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

# flatbuffers is public - exposed to tt_metal by tt_simulation_device_generated.h
target_link_libraries(
    device
    PUBLIC
        flatbuffers
    PRIVATE
        umd::Common
        umd::Firmware
        numa
        nng
        rt
        uv_a
        Boost::interprocess
        spdlog::spdlog_header_only
        fmt::fmt-header-only
        yaml-cpp::yaml-cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/${CMAKE_SYSTEM_PROCESSOR}/libcreate_ethernet_map.a
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
