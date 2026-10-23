# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

# Workload driver API (Cluster and Chips), built on top of the UMD core (tt-umd-base). Together they make the tt-umd
# library, which is what consumers link.
if(TT_UMD_BUILD_STATIC)
    add_library(tt-umd STATIC)
else()
    add_library(tt-umd SHARED)
endif()
add_library(${PROJECT_NAME}::tt-umd ALIAS tt-umd)

set_target_properties(
    tt-umd
    PROPERTIES
        VERSION
            ${umd_VERSION}
        SOVERSION
            ${umd_VERSION_MAJOR}
)

# Enable unity builds for faster compilation
# Note: Requires TT_ENABLE_UNITY_BUILD function from parent project
if(COMMAND TT_ENABLE_UNITY_BUILD)
    tt_enable_unity_build(tt-umd)
endif()

target_sources(
    tt-umd
    PRIVATE
        src/chip/chip.cpp
        src/chip/local_chip.cpp
        src/chip/mock_chip.cpp
        src/chip/remote_chip.cpp
        src/cluster.cpp
)

target_include_directories(
    tt-umd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        # Temporary: the workload layer should not depend on tt-umd private headers, see #3719
        ${PROJECT_SOURCE_DIR}/tt-umd/src
        ${PROJECT_SOURCE_DIR}/tt-umd/src/common
)

# tt-umd-base is linked publicly within this build only. Its objects are part of this library, and exporting it would
# also export its private dependencies, so its usage requirements for installed consumers are declared on this target.
target_link_libraries(
    tt-umd
    PUBLIC
        $<BUILD_INTERFACE:tt-umd-base>
        fmt::fmt-header-only
    PRIVATE
        spdlog::spdlog_header_only
        tt-logger::tt-logger
        tt-umd-utils::tt-umd-utils
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(tt-umd PRIVATE src/simulation/simulation_chip.cpp)
    target_compile_definitions(tt-umd PUBLIC TT_UMD_BUILD_SIMULATION)
endif()

if(TT_UMD_BUILD_EMULE)
    target_sources(tt-umd PRIVATE src/chip/sw_emule_chip.cpp)
    target_compile_definitions(tt-umd PRIVATE TT_UMD_BUILD_EMULE)
    target_link_libraries(tt-umd PRIVATE tt-emule::headers)
endif()

if(TT_UMD_ENABLE_TRACY)
    target_link_libraries(tt-umd PRIVATE TracyClient)
    target_compile_definitions(tt-umd PRIVATE TRACY_ENABLE)
endif()

target_sources(
    tt-umd
    PUBLIC
        FILE_SET api
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
        FILES
            api/tt-umd-workload/chip/chip.hpp
            api/tt-umd-workload/chip/local_chip.hpp
            api/tt-umd-workload/chip/mock_chip.hpp
            api/tt-umd-workload/chip/remote_chip.hpp
            api/tt-umd-workload/chip/sw_emule_chip.hpp
            api/tt-umd-workload/cluster.hpp
            api/tt-umd-workload/simulation/simulation_chip.hpp
)

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
