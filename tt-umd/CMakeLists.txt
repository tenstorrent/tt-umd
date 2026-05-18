cmake_minimum_required(VERSION 3.25)
project(tt-umd)
set(POSITION_INDEPENDENT_CODE ON)
include(CheckFunctionExists)

if(TT_UMD_BUILD_SIMULATION)
    set(FBS_FILE ${PROJECT_SOURCE_DIR}/tt-umd/src/simulation/simulation_device.fbs)
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

add_library(tt-umd-api INTERFACE)
add_library(${PROJECT_NAME}::tt-umd-api ALIAS tt-umd-api)
target_include_directories(tt-umd-api INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>)

if(TT_UMD_BUILD_STATIC)
    add_library(tt-umd STATIC)
    message(STATUS "Building tt-umd library as STATIC")
else()
    add_library(tt-umd SHARED)
    message(STATUS "Building tt-umd library as SHARED")
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
    # Exclude files that cause ODR violations due to conflicting eth_interface.h headers
    set_source_files_properties(
        src/arch/wormhole_implementation.cpp
        src/arch/blackhole_implementation.cpp
        PROPERTIES
            SKIP_UNITY_BUILD_INCLUSION
                ON
    )
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
                api/tt-umd/arc/arc_messenger.hpp
                api/tt-umd/arc/arc_telemetry_reader.hpp
                api/tt-umd/arc/smbus_arc_telemetry_reader.hpp
                api/tt-umd/arch/blackhole_implementation.hpp
                api/tt-umd/chip_helpers/sysmem_manager.hpp
                api/tt-umd/chip_helpers/simulation_sysmem_manager.hpp
                api/tt-umd/chip_helpers/silicon_sysmem_manager.hpp
                api/tt-umd/chip_helpers/sysmem_buffer.hpp
                api/tt-umd/chip_helpers/tlb_manager.hpp
                api/tt-umd/chip_helpers/simulation_tlb_manager.hpp
                api/tt-umd/coordinates/coordinate_manager.hpp
                api/tt-umd/driver_atomics.hpp
                api/tt-umd/logging/config.hpp
                api/tt-umd/utils/lock_manager.hpp
                api/tt-umd/utils/kmd_versions.hpp
                api/tt-umd/pcie/pci_device.hpp
                api/tt-umd/pcie/pci_ids.h
                api/tt-umd/pcie/tlb_handle.hpp
                api/tt-umd/pcie/tlb_window.hpp
                api/tt-umd/warm_reset.hpp
                api/tt-umd/utils/semver.hpp
                api/tt-umd/utils/robust_mutex.hpp
                api/tt-umd/cluster_descriptor.hpp
                api/tt-umd/types/core_coordinates.hpp
                api/tt-umd/tt_device/tt_device.hpp
                api/tt-umd/tt_device/protocol/device_protocol.hpp
                api/tt-umd/tt_device/remote_communication.hpp
                api/tt-umd/tt_device/remote_communication_legacy_firmware.hpp
                api/tt-umd/firmware/firmware_info_provider.hpp
                api/tt-umd/firmware/firmware_telemetry_mapping.hpp
                api/tt-umd/firmware/firmware_utils.hpp
                api/tt-umd/tt_io.hpp
                api/tt-umd/types/tensix_soft_reset_options.hpp
                api/tt-umd/types/noc_id.hpp
                api/tt-umd/types/risc_type.hpp
                api/tt-umd/soc_descriptor.hpp
                api/tt-umd/topology/topology_discovery.hpp
                api/tt-umd/topology/topology_utils.hpp
                api/tt-umd/topology/topology_discovery_options.hpp
                api/tt-umd/utils/common.hpp
                api/tt-umd/utils/error_detail.hpp
                api/tt-umd/utils/error.hpp
                api/tt-umd/utils/timeouts.hpp
                api/tt-umd/types/arch.hpp
                api/tt-umd/types/blackhole_arc.hpp
                api/tt-umd/types/blackhole_eth.hpp
                api/tt-umd/types/cluster_descriptor_types.hpp
                api/tt-umd/types/cluster_types.hpp
                api/tt-umd/types/communication_protocol.hpp
                api/tt-umd/types/core_coordinates.hpp
                api/tt-umd/types/gddr_telemetry.hpp
                api/tt-umd/types/telemetry.hpp
                api/tt-umd/types/tlb.hpp
                api/tt-umd/types/wormhole_dram.hpp
                api/tt-umd/types/wormhole_eth.hpp
                api/tt-umd/types/wormhole_telemetry.hpp
                api/tt-umd/types/xy_pair.hpp
                api/tt-umd/arch/wormhole_implementation.hpp
                api/tt-umd/arch/grendel_implementation.hpp
                api/tt-umd/arc/spi_tt_device.hpp
    )
endif()

target_include_directories(tt-umd PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

target_link_libraries(
    tt-umd
    PUBLIC
        tt-umd-api
    PRIVATE
        umd::Firmware
        hwloc
        rt
        spdlog::spdlog_header_only
        fmt::fmt-header-only
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
    message(STATUS "Tracy profiling enabled for tt-umd library")
endif()

# Install functionality requires CMake 3.23+ for FILE_SET support
# Skip install for static libraries as they are typically used for internal linking only
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(
        TARGETS
            tt-umd-api
        EXPORT ${PROJECT_NAME}Targets
        COMPONENT
        umd-dev
    )
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

add_subdirectory(src)
