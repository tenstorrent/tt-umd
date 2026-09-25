cmake_minimum_required(VERSION 3.16)

set(POSITION_INDEPENDENT_CODE ON)
include(CheckFunctionExists)

if(TT_UMD_BUILD_SIMULATION)
    set(FBS_FILES
        ${PROJECT_SOURCE_DIR}/tt-umd/src/simulation/simulation_device.fbs
        ${PROJECT_SOURCE_DIR}/tt-umd/src/simulation/simulation_server_protocol.fbs
    )
    set(FBS_DEPENDS "")
    if(TARGET flatc)
        set(FBS_DEPENDS flatc)
    endif()
    set(FBS_GENERATED_HEADERS "")
    foreach(FBS_FILE ${FBS_FILES})
        get_filename_component(FBS_FILE_NAME ${FBS_FILE} NAME_WLE)
        set(FBS_GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${FBS_FILE_NAME}_generated.h")
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
        list(APPEND FBS_GENERATED_HEADERS ${FBS_GENERATED_HEADER})
    endforeach()

    # Add FlatBuffers headers to the code analysis target
    add_custom_target(umd_device_generated_files DEPENDS ${FBS_GENERATED_HEADERS})
    add_dependencies(umd_all_generated_files umd_device_generated_files)
endif()

if(TT_UMD_BUILD_STATIC)
    add_library(tt-umd STATIC)
else()
    add_library(tt-umd SHARED)
endif()
add_library(${PROJECT_NAME}::tt-umd ALIAS tt-umd)

# Set version
set_target_properties(
    tt-umd
    PROPERTIES
        VERSION
            ${CMAKE_PROJECT_VERSION}
        SOVERSION
            ${CMAKE_PROJECT_VERSION_MAJOR}
)

# Enable unity builds for faster compilation 
# Note: Requires TT_ENABLE_UNITY_BUILD function from parent project 
if(COMMAND TT_ENABLE_UNITY_BUILD)
    tt_enable_unity_build(tt-umd)
endif()

target_sources(
    tt-umd
    PRIVATE
        src/arch/architecture_implementation.cpp
        src/arch/architecture_registers.cpp
        src/arch/architecture_tlbs.cpp
        src/chip/chip.cpp
        src/chip/local_chip.cpp
        src/chip/mock_chip.cpp
        src/chip/remote_chip.cpp
        $<$<BOOL:${TT_UMD_BUILD_EMULE}>:src/chip/sw_emule_chip.cpp>
        src/chip_helpers/sysmem_manager.cpp
        src/chip_helpers/simulation_sysmem_manager.cpp
        src/chip_helpers/silicon_sysmem_manager.cpp
        src/chip_helpers/sysmem_buffer.cpp
        src/pcie/tlb_window.cpp
        src/pcie/io_window_reconfigure.cpp
        src/io_window/io_window_target.cpp
        src/pcie/device_memcpy.cpp
        src/pcie/silicon_tlb_window.cpp
        src/pcie/silicon_tlb_handle.cpp
        src/cluster.cpp
        src/tt_device_model/wormhole_tt_device_model.cpp
        src/tt_device_model/blackhole_tt_device_model.cpp
        src/tt_device/ethernet_broadcast.cpp
        src/coordinates/coordinate_manager.cpp
        src/cpuset_lib.cpp
        src/arch/wormhole_implementation.cpp
        src/arch/blackhole_implementation.cpp
        src/arch/grendel_implementation.cpp
        src/hugepage.cpp
        src/logging/config.cpp
        src/pcie/pci_device.cpp
        src/types/tlb.cpp
        src/cluster_descriptor.cpp
        src/tt_device/tt_device.cpp
        src/tt_device/tt_device_error.cpp
        src/tt_device/hang_detection/hang_detector.cpp
        src/tt_device/hang_detection/hang_detector_implementation.cpp
        src/tt_device/hang_detection/wormhole_hang_detector.cpp
        src/tt_device/hang_detection/blackhole_hang_detector.cpp
        src/tt_device/firmware/blackhole_arc_apb.cpp
        src/tt_device/firmware/blackhole_device_firmware.cpp
        src/tt_device/firmware/wormhole_arc_window.cpp
        src/tt_device/firmware/wormhole_device_firmware.cpp
        src/tt_device/protocol/pcie_protocol.cpp
        src/tt_device/protocol/jtag_protocol.cpp
        src/tt_device/protocol/remote_protocol.cpp
        src/tt_device/protocol/pcie_dma/wormhole_dma_transfer.cpp
        src/tt_device/protocol/pcie_dma/blackhole_dma_transfer.cpp
        src/firmware/firmware_info_provider_implementation.cpp
        src/firmware/firmware_utils.cpp
        src/types/risc_type.cpp
        src/soc_arch_descriptor.cpp
        src/soc_descriptor.cpp
        src/coordinates/wormhole_coordinate_manager.cpp
        src/coordinates/blackhole_coordinate_manager.cpp
        src/coordinates/quasar_coordinate_manager.cpp
        src/coordinates/att/att_resolver.cpp
        src/arc/arc_telemetry_reader.cpp
        src/arc/wormhole_arc_telemetry_reader.cpp
        src/arc/blackhole_arc_telemetry_reader.cpp
        src/arc/smbus_arc_telemetry_reader.cpp
        src/arc/blackhole_arc_message_queue.cpp
        src/arc/spi_tt_device.cpp
        src/noc_access.cpp
        src/warm_reset.cpp
        src/warm_reset_with_recovery.cpp
        src/types/xy_pair.cpp
        src/utils/lock_manager.cpp
        src/utils/robust_mutex.cpp
        src/utils/kmd_mutex.cpp
        src/topology/topology_discovery.cpp
        src/topology/topology_discovery_blackhole.cpp
        src/topology/topology_discovery_wormhole.cpp
        src/topology/topology_discovery_error.cpp
        src/tt_device/remote_communication.cpp
        src/tt_device/remote_communication_legacy_firmware.cpp
        src/jtag/jtag_device.cpp
        src/jtag/jtag.cpp
        src/arc/wormhole_spi_tt_device.cpp
        src/arc/blackhole_spi_tt_device.cpp
        # UNIX domain socket (asio, already a core dependency) + filesystem; no simulation deps,
        # so always built. This lets its unit tests run in the no-card baremetal CI lane
        # (TT_UMD_BUILD_SIMULATION=OFF).
        src/simulation/simulation_server_socket.cpp
        src/simulation/simulation_client.cpp
        # Length-prefixed message framing over the socket (uses asio, a core dependency; no
        # FlatBuffers), so it is always built and unit-tested in the same no-card baremetal lane.
        src/simulation/simulation_server_transport.cpp
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(
        tt-umd
        PRIVATE
            src/simulation/simulation_chip.cpp
            src/simulation/simulation_connector.cpp
            src/simulation/simulation_server_protocol.cpp
            src/simulation/simulation_device_identity.cpp
            src/simulation/simulation_host.cpp
            src/simulation/tt_sim_communicator.cpp
            src/simulation/rtl_sim_communicator.cpp
            src/tt_device/simulation_tt_device.cpp
            src/tt_device/rtl_simulation_tt_device.cpp
            src/tt_device/tt_sim_tt_device.cpp
            src/chip_helpers/simulation_tlb_allocator.cpp
            src/tt_device/protocol/tt_sim_protocol.cpp
            # Composes the protocol above, so it cannot be built without it. Every user of it is a
            # simulation device, all of which are in this block too.
            src/tt_device_model/simulation_tt_device_model.cpp
            src/tt_device/simulation_device_factory.cpp
            src/pcie/tt_sim_tlb_window.cpp
            src/pcie/tt_sim_tlb_handle.cpp
            src/pcie/rtl_sim_tlb_window.cpp
            src/pcie/rtl_sim_tlb_handle.cpp
            ${FBS_GENERATED_HEADERS}
    )
    target_compile_definitions(tt-umd PUBLIC TT_UMD_BUILD_SIMULATION)
endif()

if(TT_UMD_BUILD_EMULE)
    target_compile_definitions(tt-umd PRIVATE TT_UMD_BUILD_EMULE)
    target_link_libraries(tt-umd PRIVATE tt-emule::headers)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-umd
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES
                api/tt-umd/arch/architecture_implementation.hpp
                api/tt-umd/arch/architecture_registers.hpp
                api/tt-umd/arch/architecture_tlbs.hpp
                api/tt-umd/arc/arc_telemetry_reader.hpp
                api/tt-umd/arc/firmware_telemetry_reader.hpp
                api/tt-umd/arc/blackhole_arc_message_queue.hpp
                api/tt-umd/arc/smbus_arc_telemetry_reader.hpp
                api/tt-umd/coordinates/att/att_map.hpp
                api/tt-umd/coordinates/att/att_window.hpp
                api/tt-umd/coordinates/att/att_resolver.hpp
                api/tt-umd/coordinates/att/configs/grendel_qsr1_att_map.hpp
                api/tt-umd/arch/blackhole_implementation.hpp
                api/tt-umd/chip/chip.hpp
                api/tt-umd/chip/local_chip.hpp
                api/tt-umd/chip/mock_chip.hpp
                api/tt-umd/chip/sw_emule_chip.hpp
                api/tt-umd/chip/remote_chip.hpp
                api/tt-umd/chip_helpers/sysmem_manager.hpp
                api/tt-umd/chip_helpers/simulation_sysmem_manager.hpp
                api/tt-umd/chip_helpers/silicon_sysmem_manager.hpp
                api/tt-umd/chip_helpers/system_memory_allocator.hpp
                api/tt-umd/chip_helpers/sysmem_buffer.hpp
                api/tt-umd/chip_helpers/simulation_tlb_allocator.hpp
                api/tt-umd/cluster.hpp
                api/tt-umd/coordinates/coordinate_manager.hpp
                api/tt-umd/logging/config.hpp
                api/tt-umd/utils/lock_manager.hpp
                api/tt-umd/utils/kmd_versions.hpp
                api/tt-umd/driver_atomics.hpp
                api/tt-umd/utils/mutex_interface.hpp
                api/tt-umd/pcie/pci_device.hpp
                api/tt-umd/pcie/silicon_tlb_window.hpp
                api/tt-umd/pcie/tlb_window.hpp
                api/tt-umd/pcie/tlb_handle.hpp
                api/tt-umd/pcie/silicon_tlb_handle.hpp
                api/tt-umd/io_window/io_window.hpp
                api/tt-umd/warm_reset.hpp
                api/tt-umd/warm_reset_with_recovery.hpp
                api/tt-umd/utils/semver.hpp
                api/tt-umd/utils/robust_mutex.hpp
                api/tt-umd/utils/kmd_mutex.hpp
                api/tt-umd/cluster_descriptor.hpp
                api/tt-umd/types/core_coordinates.hpp
                api/tt-umd/tt_device/tt_device.hpp
                api/tt-umd/tt_device/tt_device_error.hpp
                api/tt-umd/tt_device/hang_detection/hang_detector.hpp
                api/tt-umd/tt_device_model/tt_device_model.hpp
                api/tt-umd/types/eth_training_status.hpp
                api/tt-umd/tt_device/rtl_simulation_tt_device.hpp
                api/tt-umd/tt_device/tt_sim_tt_device.hpp
                api/tt-umd/tt_device/simulation_tt_device.hpp
                api/tt-umd/simulation/rtl_sim_communicator.hpp
                api/tt-umd/simulation/simulation_host.hpp
                api/tt-umd/tt_device/simulation_device_factory.hpp
                api/tt-umd/tt_device/protocol/device_protocol.hpp
                api/tt-umd/tt_device/protocol/pcie_interface.hpp
                api/tt-umd/tt_device/protocol/jtag_interface.hpp
                api/tt-umd/tt_device/protocol/remote_interface.hpp
                api/tt-umd/tt_device/firmware/blackhole_arc_apb.hpp
                api/tt-umd/tt_device/firmware/device_firmware.hpp
                api/tt-umd/tt_device/firmware/wormhole_arc_window.hpp
                api/tt-umd/tt_device/remote_communication.hpp
                api/tt-umd/tt_device/remote_communication_legacy_firmware.hpp
                api/tt-umd/tt_device/ethernet_broadcast.hpp
                api/tt-umd/firmware/firmware_info_provider.hpp
                api/tt-umd/firmware/firmware_info_provider_implementation.hpp
                api/tt-umd/firmware/firmware_telemetry_mapping.hpp
                api/tt-umd/firmware/firmware_utils.hpp
                api/tt-umd/types/noc_id.hpp
                api/tt-umd/types/power_state.hpp
                api/tt-umd/types/risc_type.hpp
                api/tt-umd/simulation/simulation_chip.hpp
                api/tt-umd/simulation/simulation_connector.hpp
                api/tt-umd/simulation/simulation_client.hpp
                api/tt-umd/simulation/simulation_device_identity.hpp
                api/tt-umd/simulation/simulation_server_protocol.hpp
                api/tt-umd/simulation/tt_sim_communicator.hpp
                api/tt-umd/soc_arch_descriptor.hpp
                api/tt-umd/soc_descriptor.hpp
                api/tt-umd/topology/topology_discovery.hpp
                api/tt-umd/topology/topology_discovery_options.hpp
                api/tt-umd/topology/topology_discovery_error.hpp
                api/tt-umd/utils/common.hpp
                api/tt-umd/utils/error.hpp
                api/tt-umd/utils/error_detail.hpp
                api/tt-umd/utils/mmio_timeout_config.hpp
                api/tt-umd/utils/op_timeout_guard.hpp
                api/tt-umd/utils/timeouts.hpp
                api/tt-umd/types/arch.hpp
                api/tt-umd/types/blackhole_arc.hpp
                api/tt-umd/types/blackhole_eth.hpp
                api/tt-umd/types/blackhole_l1.hpp
                api/tt-umd/types/cluster_descriptor_types.hpp
                api/tt-umd/types/cluster_types.hpp
                api/tt-umd/types/communication_protocol.hpp
                api/tt-umd/types/core_coordinates.hpp
                api/tt-umd/types/gddr_telemetry.hpp
                api/tt-umd/types/host_memory.hpp
                api/tt-umd/types/telemetry.hpp
                api/tt-umd/types/tlb.hpp
                api/tt-umd/types/io_window_config.hpp
                api/tt-umd/types/wormhole_eth.hpp
                api/tt-umd/types/wormhole_l1.hpp
                api/tt-umd/types/wormhole_telemetry.hpp
                api/tt-umd/types/xy_pair.hpp
                api/tt-umd/arch/wormhole_implementation.hpp
                api/tt-umd/jtag/jtag_device.hpp
                api/tt-umd/jtag/jtag.hpp
                api/tt-umd/arch/grendel_implementation.hpp
                api/tt-umd/arc/spi_tt_device.hpp
    )
endif()

target_include_directories(
    tt-umd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/src/common
)

target_link_libraries(
    tt-umd
    PUBLIC
        fmt::fmt-header-only
    PRIVATE
        hwloc
        rt
        spdlog::spdlog_header_only
        tt-logger::tt-logger
        yaml-cpp::yaml-cpp
)

target_link_libraries(tt-umd PRIVATE umd_asio_lib)

target_link_libraries(tt-umd PRIVATE tt-kmd-lib::tt-kmd-lib)

check_function_exists(
    backtrace
    HAVE_BACKTRACE
)

if(NOT HAVE_BACKTRACE)
    find_library(EXECINFO_LIB execinfo)
    if(EXECINFO_LIB)
        target_link_libraries(tt-umd PRIVATE ${EXECINFO_LIB})
    endif()
endif()

if(TT_UMD_BUILD_SIMULATION)
    target_link_libraries(
        tt-umd
        PRIVATE
            flatbuffers::flatbuffers
            nng
            uv_a
    )
endif()

if(TT_UMD_ENABLE_TRACY)
    target_link_libraries(tt-umd PRIVATE TracyClient)
    target_compile_definitions(tt-umd PRIVATE TRACY_ENABLE)
endif()

# Install functionality requires CMake 3.23+ for FILE_SET support
# Skip install for static libraries as they are typically used for internal linking only
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(
        TARGETS
            tt-umd
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

if(TT_UMD_BUILD_STATIC)
    set_target_properties(
        tt-umd
        PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY
                ${PROJECT_BINARY_DIR}/lib
    )
else()
    set_target_properties(
        tt-umd
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY
                ${PROJECT_BINARY_DIR}/lib
    )
endif()
