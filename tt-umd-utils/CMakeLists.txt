# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# General purpose utilities, not specific to Tenstorrent hardware, shared by the UMD components.
add_library(tt-umd-utils STATIC)
add_library(tt-umd-utils::tt-umd-utils ALIAS tt-umd-utils)

target_sources(tt-umd-utils PRIVATE src/robust_mutex.cpp)

target_include_directories(
    tt-umd-utils
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
    tt-umd-utils
    PUBLIC
        fmt::fmt-header-only
    PRIVATE
        rt
        tt-logger::tt-logger
)

# Not installed as a package of its own: it ships inside tt-umd, whose headers include these.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT umd-dev)
endif()

if(TT_UMD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
