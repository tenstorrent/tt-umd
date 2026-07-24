# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

# Allow tt-kmd-lib to be built on its own, not only via add_subdirectory().
if(NOT DEFINED PROJECT_NAME)
    project(tt-kmd-lib DESCRIPTION "Tenstorrent kernel-mode driver userspace library" LANGUAGES C)
    include(GNUInstallDirs)
endif()

add_library(tt-kmd-lib STATIC)
add_library(tt-kmd-lib::tt-kmd-lib ALIAS tt-kmd-lib)

target_sources(tt-kmd-lib PRIVATE src/tt_kmd_lib.c)

target_include_directories(
    tt-kmd-lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(tt-kmd-lib PRIVATE c_std_11)
set_target_properties(
    tt-kmd-lib
    PROPERTIES
        POSITION_INDEPENDENT_CODE
            ON
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-kmd-lib
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES api/tt-kmd-lib/tt_kmd_lib.h api/tt-kmd-lib/pci_ids.h
    )
endif()
