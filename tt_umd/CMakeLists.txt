cmake_minimum_required(VERSION 3.16)

set(POSITION_INDEPENDENT_CODE ON)
include(CheckFunctionExists)

if(TT_UMD_BUILD_SIMULATION)
    set(FBS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/simulation/simulation_device.fbs)
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

    # Add FlatBuffers header to the code analysis target
    add_custom_target(umd_device_generated_files DEPENDS ${FBS_GENERATED_HEADER})
    add_dependencies(umd_all_generated_files umd_device_generated_files)
endif()

if(TT_UMD_BUILD_STATIC)
    add_library(tt-umd STATIC)
    message(STATUS "Building tt-umd library as STATIC")
else()
    add_library(tt-umd SHARED)
    message(STATUS "Building tt-umd library as SHARED")
endif()
add_library(${PROJECT_NAME}::tt-umd ALIAS tt-umd)
# Alias for old name
add_library(${PROJECT_NAME}::device ALIAS tt-umd)
add_library(device ALIAS tt-umd)

# Enable unity builds for faster compilation 
# Note: Requires TT_ENABLE_UNITY_BUILD function from parent project 
if(COMMAND TT_ENABLE_UNITY_BUILD)
    tt_enable_unity_build(tt-umd)
    # Exclude files that cause ODR violations due to conflicting eth_interface.h headers 
    set_source_files_properties(
        src/arch/wormhole_implementation.cpp
        src/arch/blackhole_implementation.cpp
        PROPERTIES
            SKIP_UNITY_BUILD_INCLUSION
                ON
    )
endif()

target_sources(
    tt-umd
    PRIVATE
        src/arch/architecture_implementation.cpp
        src/chip_helpers/sysmem_manager.cpp
        src/chip_helpers/simulation_sysmem_manager.cpp
        src/chip_helpers/silicon_sysmem_manager.cpp
        src/chip_helpers/sysmem_buffer.cpp
        src/chip_helpers/tlb_manager.cpp
        src/pcie/tlb_window.cpp
        src/pcie/silicon_tlb_window.cpp
        src/pcie/silicon_tlb_handle.cpp
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
        src/tt_device/blackhole_tt_device.cpp
        src/tt_device/tt_device.cpp
        src/tt_device/wormhole_tt_device.cpp
        src/tt_device/remote_wormhole_tt_device.cpp
        src/tt_device/protocol/pcie_protocol.cpp
        src/tt_device/protocol/jtag_protocol.cpp
        src/tt_device/protocol/remote_protocol.cpp
        src/tt_device/protocol/pcie_dma/wormhole_dma_transfer.cpp
        src/tt_device/protocol/pcie_dma/blackhole_dma_transfer.cpp
        src/firmware/firmware_info_provider.cpp
        src/firmware/firmware_utils.cpp
        src/types/tensix_soft_reset_options.cpp
        src/types/risc_type.cpp
        src/soc_descriptor.cpp
        src/coordinates/wormhole_coordinate_manager.cpp
        src/coordinates/blackhole_coordinate_manager.cpp
        src/arc/arc_telemetry_reader.cpp
        src/arc/wormhole_arc_telemetry_reader.cpp
        src/arc/blackhole_arc_telemetry_reader.cpp
        src/arc/smbus_arc_telemetry_reader.cpp
        src/arc/arc_messenger.cpp
        src/arc/wormhole_arc_messenger.cpp
        src/arc/blackhole_arc_messenger.cpp
        src/arc/blackhole_arc_message_queue.cpp
        src/arc/spi_tt_device.cpp
        src/noc_access.cpp
        src/warm_reset.cpp
        src/types/xy_pair.cpp
        src/utils/error.cpp
        src/utils/lock_manager.cpp
        src/utils/robust_mutex.cpp
        src/topology/topology_discovery.cpp
        src/topology/topology_discovery_blackhole.cpp
        src/topology/topology_discovery_wormhole.cpp
        src/tt_device/remote_communication.cpp
        src/tt_device/remote_communication_legacy_firmware.cpp
        src/jtag/jtag_device.cpp
        src/jtag/jtag.cpp
        src/arc/wormhole_spi_tt_device.cpp
        src/arc/blackhole_spi_tt_device.cpp
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(
        tt-umd
        PRIVATE
            src/
            simulation/simulation_host.cpp
            src/
            simulation/tt_sim_communicator.cpp
            src/
            simulation/rtl_sim_communicator.cpp
            src/
            tt_device/rtl_simulation_tt_device.cpp
            src/
            tt_device/tt_sim_tt_device.cpp
            src/
            chip_helpers/simulation_tlb_manager.cpp
            src/
            tt_device/simulation_device_factory.cpp
            src/
            pcie/tt_sim_tlb_window.cpp
            src/
            pcie/tt_sim_tlb_handle.cpp
            src/
            pcie/rtl_sim_tlb_window.cpp
            src/
            pcie/rtl_sim_tlb_handle.cpp
            ${FBS_GENERATED_HEADER}
    )
    target_compile_definitions(tt-umd PRIVATE TT_UMD_BUILD_SIMULATION)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-umd
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES
                api/tt_umd/arch/architecture_implementation.hpp
                api/tt_umd/arc/arc_messenger.hpp
                api/tt_umd/arc/arc_telemetry_reader.hpp
                api/tt_umd/arc/blackhole_arc_message_queue.hpp
                api/tt_umd/arc/blackhole_arc_messenger.hpp
                api/tt_umd/arc/blackhole_arc_telemetry_reader.hpp
                api/tt_umd/arc/blackhole_spi_tt_device.hpp
                api/tt_umd/arc/smbus_arc_telemetry_reader.hpp
                api/tt_umd/arc/wormhole_arc_messenger.hpp
                api/tt_umd/arc/wormhole_arc_telemetry_reader.hpp
                api/tt_umd/arc/wormhole_spi_tt_device.hpp
                api/tt_umd/coordinates/blackhole_coordinate_manager.hpp
                api/tt_umd/arch/blackhole_implementation.hpp
                api/tt_umd/chip_helpers/sysmem_manager.hpp
                api/tt_umd/chip_helpers/simulation_sysmem_manager.hpp
                api/tt_umd/chip_helpers/silicon_sysmem_manager.hpp
                api/tt_umd/chip_helpers/sysmem_buffer.hpp
                api/tt_umd/chip_helpers/tlb_manager.hpp
                api/tt_umd/chip_helpers/simulation_tlb_manager.hpp
                api/tt_umd/coordinates/coordinate_manager.hpp
                api/tt_umd/driver_atomics.hpp
                api/tt_umd/logging/config.hpp
                api/tt_umd/utils/lock_manager.hpp
                api/tt_umd/utils/kmd_versions.hpp
                api/tt_umd/pcie/pci_device.hpp
                api/tt_umd/pcie/tlb_handle.hpp
                api/tt_umd/pcie/silicon_tlb_handle.hpp
                api/tt_umd/pcie/silicon_tlb_window.hpp
                api/tt_umd/pcie/tlb_window.hpp
                api/tt_umd/pcie/tt_sim_tlb_window.hpp
                api/tt_umd/pcie/tt_sim_tlb_handle.hpp
                api/tt_umd/pcie/rtl_sim_tlb_window.hpp
                api/tt_umd/pcie/rtl_sim_tlb_handle.hpp
                api/tt_umd/warm_reset.hpp
                api/tt_umd/utils/semver.hpp
                api/tt_umd/utils/robust_mutex.hpp
                api/tt_umd/cluster_descriptor.hpp
                api/tt_umd/types/core_coordinates.hpp
                api/tt_umd/tt_device/blackhole_tt_device.hpp
                api/tt_umd/tt_device/tt_device.hpp
                api/tt_umd/tt_device/wormhole_tt_device.hpp
                api/tt_umd/tt_device/remote_wormhole_tt_device.hpp
                api/tt_umd/tt_device/rtl_simulation_tt_device.hpp
                api/tt_umd/tt_device/tt_sim_tt_device.hpp
                api/tt_umd/tt_device/simulation_device_factory.hpp
                api/tt_umd/tt_device/protocol/device_protocol.hpp
                api/tt_umd/tt_device/protocol/pcie_interface.hpp
                api/tt_umd/tt_device/protocol/pcie_protocol.hpp
                api/tt_umd/tt_device/protocol/jtag_interface.hpp
                api/tt_umd/tt_device/protocol/jtag_protocol.hpp
                api/tt_umd/tt_device/protocol/remote_interface.hpp
                api/tt_umd/tt_device/protocol/remote_protocol.hpp
                api/tt_umd/tt_device/protocol/pcie_dma/dma_transfer.hpp
                api/tt_umd/tt_device/protocol/pcie_dma/wormhole_dma_transfer.hpp
                api/tt_umd/tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp
                api/tt_umd/tt_device/remote_communication.hpp
                api/tt_umd/tt_device/remote_communication_legacy_firmware.hpp
                api/tt_umd/firmware/erisc_firmware.hpp
                api/tt_umd/firmware/firmware_info_provider.hpp
                api/tt_umd/firmware/firmware_telemetry_mapping.hpp
                api/tt_umd/firmware/firmware_utils.hpp
                api/tt_umd/tt_io.hpp
                api/tt_umd/types/tensix_soft_reset_options.hpp
                api/tt_umd/types/noc_id.hpp
                api/tt_umd/types/risc_type.hpp
                api/tt_umd/simulation/simulation_host.hpp
                api/tt_umd/simulation/tt_sim_communicator.hpp
                api/tt_umd/simulation/rtl_sim_communicator.hpp
                api/tt_umd/soc_descriptor.hpp
                api/tt_umd/topology/topology_discovery_blackhole.hpp
                api/tt_umd/topology/topology_discovery_wormhole.hpp
                api/tt_umd/topology/topology_discovery.hpp
                api/tt_umd/topology/topology_utils.hpp
                api/tt_umd/topology/topology_discovery_options.hpp
                api/tt_umd/utils/common.hpp
                api/tt_umd/utils/error_detail.hpp
                api/tt_umd/utils/error.hpp
                api/tt_umd/utils/timeouts.hpp
                api/tt_umd/types/arch.hpp
                api/tt_umd/types/blackhole_arc.hpp
                api/tt_umd/types/blackhole_eth.hpp
                api/tt_umd/types/cluster_descriptor_types.hpp
                api/tt_umd/types/cluster_types.hpp
                api/tt_umd/types/communication_protocol.hpp
                api/tt_umd/types/core_coordinates.hpp
                api/tt_umd/types/gddr_telemetry.hpp
                api/tt_umd/types/telemetry.hpp
                api/tt_umd/types/tlb.hpp
                api/tt_umd/types/wormhole_dram.hpp
                api/tt_umd/types/wormhole_eth.hpp
                api/tt_umd/types/wormhole_telemetry.hpp
                api/tt_umd/types/xy_pair.hpp
                api/tt_umd/coordinates/wormhole_coordinate_manager.hpp
                api/tt_umd/arch/wormhole_implementation.hpp
                api/tt_umd/jtag/jtag_device.hpp
                api/tt_umd/jtag/jtag.hpp
                api/tt_umd/arch/grendel_implementation.hpp
                api/tt_umd/arc/spi_tt_device.hpp
    )
endif()

target_include_directories(
    tt-umd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/common>
)

target_link_libraries(tt-umd PRIVATE tt_kmd_lib)

target_link_libraries(
    tt-umd
    PRIVATE
        umd::Firmware
        hwloc
        rt
        spdlog::spdlog_header_only
        fmt::fmt-header-only
        picosha2::picosha2
        tt-logger::tt-logger
        yaml-cpp::yaml-cpp
)

target_link_libraries(tt-umd PRIVATE umd_asio_lib)

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
    target_compile_definitions(tt-umd PUBLIC TRACY_ENABLE)
    message(STATUS "Tracy profiling enabled for device library")
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

# Add a custom command to copy the library to build/lib
add_custom_command(
    TARGET tt-umd
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
    COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tt-umd> ${CMAKE_BINARY_DIR}/lib/
    COMMENT "Copying tt-umd library to build/lib"
)

if(TT_UMD_BUILD_PYTHON)
    message(STATUS "Building ${PROJECT_NAME} with Python Bindings")
    add_subdirectory(nanobind)
endif()
