cmake_minimum_required(VERSION 3.25)

set(POSITION_INDEPENDENT_CODE ON)
include(CheckFunctionExists)

if(TT_UMD_BUILD_SIMULATION)
    set(FBS_FILES
        ${PROJECT_SOURCE_DIR}/device/simulation/simulation_device.fbs
        ${PROJECT_SOURCE_DIR}/device/simulation/simulation_server_protocol.fbs
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
        arch/architecture_implementation.cpp
        chip/chip.cpp
        chip/local_chip.cpp
        chip/mock_chip.cpp
        chip/remote_chip.cpp
        $<$<BOOL:${TT_UMD_BUILD_EMULE}>:chip/sw_emule_chip.cpp>
        chip_helpers/sysmem_manager.cpp
        chip_helpers/simulation_sysmem_manager.cpp
        chip_helpers/silicon_sysmem_manager.cpp
        chip_helpers/sysmem_buffer.cpp
        chip_helpers/tlb_manager.cpp
        pcie/tlb_window.cpp
        pcie/device_memcpy.cpp
        pcie/silicon_tlb_window.cpp
        pcie/silicon_tlb_handle.cpp
        cluster.cpp
        tt_device/ethernet_broadcast.cpp
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
        tt_device/tt_device_error.cpp
        tt_device/wormhole_tt_device.cpp
        tt_device/hang_detection/hang_detector.cpp
        tt_device/hang_detection/hang_detector_implementation.cpp
        tt_device/hang_detection/wormhole_hang_detector.cpp
        tt_device/hang_detection/blackhole_hang_detector.cpp
        tt_device/protocol/pcie_protocol.cpp
        tt_device/protocol/jtag_protocol.cpp
        tt_device/protocol/remote_protocol.cpp
        tt_device/protocol/pcie_dma/wormhole_dma_transfer.cpp
        tt_device/protocol/pcie_dma/blackhole_dma_transfer.cpp
        firmware/firmware_info_provider.cpp
        firmware/firmware_utils.cpp
        types/risc_type.cpp
        soc_arch_descriptor.cpp
        soc_descriptor.cpp
        coordinates/wormhole_coordinate_manager.cpp
        coordinates/blackhole_coordinate_manager.cpp
        coordinates/quasar_coordinate_manager.cpp
        arc/arc_telemetry_reader.cpp
        arc/wormhole_arc_telemetry_reader.cpp
        arc/blackhole_arc_telemetry_reader.cpp
        arc/smbus_arc_telemetry_reader.cpp
        arc/arc_messenger.cpp
        arc/wormhole_arc_messenger.cpp
        arc/blackhole_arc_messenger.cpp
        arc/blackhole_arc_message_queue.cpp
        arc/spi_tt_device.cpp
        noc_access.cpp
        warm_reset.cpp
        warm_reset_with_recovery.cpp
        types/xy_pair.cpp
        utils/lock_manager.cpp
        utils/robust_mutex.cpp
        topology/topology_discovery.cpp
        topology/topology_discovery_blackhole.cpp
        topology/topology_discovery_wormhole.cpp
        topology/topology_discovery_error.cpp
        tt_device/remote_communication.cpp
        tt_device/remote_communication_legacy_firmware.cpp
        jtag/jtag_device.cpp
        jtag/jtag.cpp
        arc/wormhole_spi_tt_device.cpp
        arc/blackhole_spi_tt_device.cpp
        # UNIX domain socket (asio, already a core dependency) + filesystem; no simulation deps,
        # so always built. This lets its unit tests run in the no-card baremetal CI lane
        # (TT_UMD_BUILD_SIMULATION=OFF).
        simulation/simulation_server_socket.cpp
        simulation/simulation_client.cpp
        # Length-prefixed message framing over the socket (uses asio, a core dependency; no
        # FlatBuffers), so it is always built and unit-tested in the same no-card baremetal lane.
        simulation/simulation_server_transport.cpp
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(
        tt-umd
        PRIVATE
            simulation/simulation_chip.cpp
            simulation/simulation_connector.cpp
            simulation/simulation_server_protocol.cpp
            simulation/simulation_device_identity.cpp
            simulation/simulation_host.cpp
            simulation/tt_sim_communicator.cpp
            simulation/rtl_sim_communicator.cpp
            tt_device/simulation_tt_device.cpp
            tt_device/rtl_simulation_tt_device.cpp
            tt_device/tt_sim_tt_device.cpp
            chip_helpers/simulation_tlb_allocator.cpp
            tt_device/simulation_device_factory.cpp
            pcie/tt_sim_tlb_window.cpp
            pcie/tt_sim_tlb_handle.cpp
            pcie/rtl_sim_tlb_window.cpp
            pcie/rtl_sim_tlb_handle.cpp
            ${FBS_GENERATED_HEADERS}
    )
    target_compile_definitions(tt-umd PUBLIC TT_UMD_BUILD_SIMULATION)
endif()

if(TT_UMD_BUILD_EMULE)
    target_compile_definitions(tt-umd PRIVATE TT_UMD_BUILD_EMULE)
    target_link_libraries(tt-umd PRIVATE tt-emule::headers)
endif()

target_include_directories(
    tt-umd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/common
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

# Skip install for static libraries as they are typically used for internal linking only
if(NOT TT_UMD_BUILD_STATIC)
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
