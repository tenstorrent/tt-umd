# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Workload driver API (Cluster and Chips) built on top of tt-umd.
# Built into the tt-umd library for now, so its consumers keep linking only tt-umd. tt-umd links this component, so
# this component takes tt-umd's headers, including the private ones, and its build settings directly.
add_library(tt-umd-workload OBJECT)
add_library(tt-umd-workload::tt-umd-workload ALIAS tt-umd-workload)

target_sources(
    tt-umd-workload
    PRIVATE
        src/chip/chip.cpp
        src/chip/local_chip.cpp
        src/chip/mock_chip.cpp
        src/chip/remote_chip.cpp
        $<$<BOOL:${TT_UMD_BUILD_EMULE}>:src/chip/sw_emule_chip.cpp>
        src/cluster.cpp
        $<$<BOOL:${TT_UMD_BUILD_SIMULATION}>:src/simulation/simulation_chip.cpp>
)

target_include_directories(
    tt-umd-workload
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/api
        ${PROJECT_SOURCE_DIR}/tt-umd/api
        ${PROJECT_SOURCE_DIR}/tt-umd/src
        ${PROJECT_SOURCE_DIR}/tt-umd/src/common
)

target_compile_definitions(tt-umd-workload PRIVATE $<TARGET_PROPERTY:tt-umd,COMPILE_DEFINITIONS>)

target_link_libraries(
    tt-umd-workload
    PRIVATE
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        tt-logger::tt-logger
        tt-umd-utils::tt-umd-utils
        tt-umd-utils::internal
        $<$<BOOL:${TT_UMD_ENABLE_TRACY}>:TracyClient>
)

if(TT_UMD_BUILD_EMULE)
    target_compile_definitions(tt-umd-workload PRIVATE TT_UMD_BUILD_EMULE)
    target_link_libraries(tt-umd-workload PRIVATE tt-emule::headers)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-umd-workload
        PRIVATE
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
endif()

# Not installed as a package of its own: it ships inside tt-umd, whose headers include these.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT umd-dev)
endif()
