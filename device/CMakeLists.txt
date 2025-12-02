cmake_minimum_required(VERSION 3.16)

set(POSITION_INDEPENDENT_CODE ON)
include(CheckFunctionExists)

if(TT_UMD_BUILD_SIMULATION)
    set(FBS_FILE ${PROJECT_SOURCE_DIR}/device/simulation/simulation_device.fbs)
    get_filename_component(FBS_FILE_NAME ${FBS_FILE} NAME_WLE)
    set(FBS_GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${FBS_FILE_NAME}_generated.h")
    set(FBS_DEPENDS "")
    if(TARGET flatc)
        set(FBS_DEPENDS flatc)
    endif()
    add_custom_command(
        OUTPUT
            ${FBS_GENERATED_HEADER}
        COMMAND
            flatc --cpp -o "${CMAKE_CURRENT_BINARY_DIR}" ${FBS_FILE}
        DEPENDS
            ${FBS_DEPENDS}
            ${FBS_FILE}
        COMMENT "Generating FlatBuffers header ${FBS_GENERATED_HEADER}"
        VERBATIM
    )
endif()

# --- Embed lite_fabric.bin as a C header ---
set(LITE_FABRIC_BIN ${CMAKE_CURRENT_SOURCE_DIR}/libs/lite_fabric.bin)
set(LITE_FABRIC_EMBED ${CMAKE_CURRENT_BINARY_DIR}/lite_fabric.embed)

add_custom_command(
    OUTPUT
        ${LITE_FABRIC_EMBED}
    COMMAND
        ${CMAKE_COMMAND} -E echo "Generating ${LITE_FABRIC_EMBED} from ${LITE_FABRIC_BIN}"
    COMMAND
        cat ${LITE_FABRIC_BIN} | xxd -i > ${LITE_FABRIC_EMBED}
    DEPENDS
        ${LITE_FABRIC_BIN}
    COMMENT "Embedding lite_fabric.bin as C array"
    VERBATIM
)

add_custom_target(generate_lite_fabric_embed ALL DEPENDS ${LITE_FABRIC_EMBED})

add_library(device SHARED)
add_library(${PROJECT_NAME}::device ALIAS device)

add_dependencies(device generate_lite_fabric_embed)

target_sources(
    device
    PRIVATE
        arch/architecture_implementation.cpp
        chip/chip.cpp
        chip/local_chip.cpp
        chip/mock_chip.cpp
        chip/remote_chip.cpp
        chip_helpers/sysmem_manager.cpp
        chip_helpers/sysmem_buffer.cpp
        chip_helpers/tlb_manager.cpp
        pcie/tlb_window.cpp
        pcie/tlb_handle.cpp
        cluster.cpp
        coordinates/coordinate_manager.cpp
        cpuset_lib.cpp
        arch/wormhole_implementation.cpp
        arch/blackhole_implementation.cpp
        arch/grendel_implementation.cpp
        hugepage.cpp
        logging/config.cpp
        pcie/pci_device.cpp
        types/tlb.cpp
        cluster_descriptor.cpp
        tt_device/blackhole_tt_device.cpp
        tt_device/tt_device.cpp
        tt_device/wormhole_tt_device.cpp
        tt_device/remote_wormhole_tt_device.cpp
        tt_device/remote_blackhole_tt_device.cpp
        tt_device/rtl_simulation_tt_device.cpp
        tt_device/tt_sim_tt_device.cpp
        firmware/firmware_info_provider.cpp
        firmware/wormhole_18_3_firmware_info_provider.cpp
        firmware/wormhole_18_7_firmware_info_provider.cpp
        firmware/blackhole_18_7_firmware_info_provider.cpp
        firmware/firmware_utils.cpp
        types/tensix_soft_reset_options.cpp
        types/risc_type.cpp
        soc_descriptor.cpp
        coordinates/wormhole_coordinate_manager.cpp
        coordinates/blackhole_coordinate_manager.cpp
        arc/arc_telemetry_reader.cpp
        arc/wormhole_arc_telemetry_reader.cpp
        arc/blackhole_arc_telemetry_reader.cpp
        arc/smbus_arc_telemetry_reader.cpp
        arc/arc_messenger.cpp
        arc/wormhole_arc_messenger.cpp
        arc/blackhole_arc_messenger.cpp
        arc/blackhole_arc_message_queue.cpp
        warm_reset.cpp
        types/xy_pair.cpp
        utils/lock_manager.cpp
        utils/robust_mutex.cpp
        topology/topology_discovery.cpp
        topology/topology_discovery_blackhole.cpp
        topology/topology_discovery_wormhole.cpp
        tt_device/remote_communication.cpp
        tt_device/remote_communication_legacy_firmware.cpp
        tt_device/remote_communication_lite_fabric.cpp
        jtag/jtag_device.cpp
        jtag/jtag.cpp
        lite_fabric/lite_fabric_host_utils.cpp
        ${LITE_FABRIC_EMBED} # include generated embed file
        tt_kmd_lib/tt_kmd_lib.c
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(
        device
        PRIVATE
            simulation/simulation_chip.cpp
            simulation/tt_sim_chip.cpp
            simulation/rtl_simulation_chip.cpp
            simulation/simulation_host.cpp
            ${FBS_GENERATED_HEADER}
    )
    target_compile_definitions(device PRIVATE TT_UMD_BUILD_SIMULATION)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        device
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES
                api/umd/device/arch/architecture_implementation.hpp
                api/umd/device/arc/arc_messenger.hpp
                api/umd/device/arc/arc_telemetry_reader.hpp
                api/umd/device/arc/blackhole_arc_message_queue.hpp
                api/umd/device/arc/blackhole_arc_messenger.hpp
                api/umd/device/arc/smbus_arc_telemetry_reader.hpp
                api/umd/device/arc/wormhole_arc_messenger.hpp
                api/umd/device/arc/wormhole_arc_telemetry_reader.hpp
                api/umd/device/coordinates/blackhole_coordinate_manager.hpp
                api/umd/device/arch/blackhole_implementation.hpp
                api/umd/device/chip/chip.hpp
                api/umd/device/chip/local_chip.hpp
                api/umd/device/chip/mock_chip.hpp
                api/umd/device/chip/remote_chip.hpp
                api/umd/device/chip_helpers/sysmem_manager.hpp
                api/umd/device/chip_helpers/sysmem_buffer.hpp
                api/umd/device/chip_helpers/tlb_manager.hpp
                api/umd/device/cluster.hpp
                api/umd/device/coordinates/coordinate_manager.hpp
                api/umd/device/driver_atomics.hpp
                api/umd/device/logging/config.hpp
                api/umd/device/utils/lock_manager.hpp
                api/umd/device/utils/kmd_versions.hpp
                api/umd/device/pcie/pci_device.hpp
                api/umd/device/pcie/tlb_handle.hpp
                api/umd/device/pcie/tlb_window.hpp
                api/umd/device/warm_reset.hpp
                api/umd/device/utils/semver.hpp
                api/umd/device/utils/robust_mutex.hpp
                api/umd/device/cluster_descriptor.hpp
                api/umd/device/types/core_coordinates.hpp
                api/umd/device/tt_device/blackhole_tt_device.hpp
                api/umd/device/tt_device/tt_device.hpp
                api/umd/device/tt_device/wormhole_tt_device.hpp
                api/umd/device/tt_device/remote_wormhole_tt_device.hpp
                api/umd/device/tt_device/remote_blackhole_tt_device.hpp
                api/umd/device/tt_device/rtl_simulation_tt_device.hpp
                api/umd/device/tt_device/tt_sim_tt_device.hpp
                api/umd/device/tt_device/remote_communication.hpp
                api/umd/device/tt_device/remote_communication_legacy_firmware.hpp
                api/umd/device/tt_device/remote_communication_lite_fabric.hpp
                api/umd/device/lite_fabric/lite_fabric.hpp
                api/umd/device/lite_fabric/fabric_edm_types.hpp
                api/umd/device/lite_fabric/lf_dev_mem_map.hpp
                api/umd/device/lite_fabric/lite_fabric_constants.hpp
                api/umd/device/lite_fabric/lite_fabric_header.hpp
                api/umd/device/lite_fabric/lite_fabric_host_utils.hpp
                api/umd/device/firmware/firmware_info_provider.hpp
                api/umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp
                api/umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp
                api/umd/device/firmware/blackhole_18_7_firmware_info_provider.hpp
                api/umd/device/firmware/firmware_utils.hpp
                api/umd/device/tt_io.hpp
                api/umd/device/types/tensix_soft_reset_options.hpp
                api/umd/device/types/risc_type.hpp
                api/umd/device/simulation/simulation_chip.hpp
                api/umd/device/simulation/tt_sim_chip.hpp
                api/umd/device/simulation/rtl_simulation_chip.hpp
                api/umd/device/simulation/simulation_host.hpp
                api/umd/device/soc_descriptor.hpp
                api/umd/device/topology/topology_discovery_blackhole.hpp
                api/umd/device/topology/topology_discovery_wormhole.hpp
                api/umd/device/topology/topology_discovery.hpp
                api/umd/device/topology/topology_utils.hpp
                api/umd/device/utils/common.hpp
                api/umd/device/utils/timeouts.hpp
                api/umd/device/types/arch.hpp
                api/umd/device/types/blackhole_arc.hpp
                api/umd/device/types/blackhole_eth.hpp
                api/umd/device/types/cluster_descriptor_types.hpp
                api/umd/device/types/cluster_types.hpp
                api/umd/device/types/communication_protocol.hpp
                api/umd/device/types/core_coordinates.hpp
                api/umd/device/types/telemetry.hpp
                api/umd/device/types/tlb.hpp
                api/umd/device/types/wormhole_dram.hpp
                api/umd/device/types/wormhole_telemetry.hpp
                api/umd/device/types/xy_pair.hpp
                api/umd/device/coordinates/wormhole_coordinate_manager.hpp
                api/umd/device/arch/wormhole_implementation.hpp
                api/umd/device/jtag/jtag_device.hpp
                api/umd/device/jtag/jtag.hpp
                api/umd/device/tt_kmd_lib/tt_kmd_lib.h
                api/umd/device/arch/grendel_implementation.hpp
    )
endif()

target_include_directories(
    device
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(
    device
    PRIVATE
        umd::Common
        umd::Firmware
        hwloc
        rt
        spdlog::spdlog_header_only
        fmt::fmt-header-only
        tt-logger::tt-logger
        yaml-cpp::yaml-cpp
)

target_link_libraries(device PRIVATE umd_asio_lib)

check_function_exists(
    backtrace
    HAVE_BACKTRACE
)

if(NOT HAVE_BACKTRACE)
    find_library(EXECINFO_LIB execinfo)
    if(EXECINFO_LIB)
        target_link_libraries(device PRIVATE ${EXECINFO_LIB})
    endif()
endif()

if(TT_UMD_BUILD_SIMULATION)
    target_link_libraries(
        device
        PRIVATE
            flatbuffers::flatbuffers
            nng
            uv_a
    )
endif()

# Install functionality requires CMake 3.23+ for FILE_SET support
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    install(
        TARGETS
            device
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT umd-dev
        LIBRARY
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT umd-runtime
        FILE_SET
        api
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT umd-dev
    )

    install(
        EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
        NAMESPACE ${PROJECT_NAME}::
        COMPONENT umd-dev
    )
else()
    # Warn user that install functionality is not available
    message(STATUS "Install functionality disabled: CMake ${CMAKE_VERSION} < 3.23 required for FILE_SET support")
endif()

# Add a custom command to copy the library to build/lib
add_custom_command(
    TARGET device
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
    COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:device> ${CMAKE_BINARY_DIR}/lib/
    COMMENT "Copying device library to build/lib"
)

install(
    DIRECTORY
        ${PROJECT_SOURCE_DIR}/common
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/common
    COMPONENT umd-dev
    FILES_MATCHING
    PATTERN
    "*.h"
    PATTERN
    "*.hpp"
)
