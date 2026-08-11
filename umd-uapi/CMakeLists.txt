# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

# Allow umd-uapi to be built on its own, not only via add_subdirectory().
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(umd-uapi DESCRIPTION "Tenstorrent kernel-mode driver userspace library" LANGUAGES C)
    include(GNUInstallDirs)
endif()

add_library(umd-uapi STATIC)
add_library(umd-uapi::umd-uapi ALIAS umd-uapi)

target_sources(umd-uapi PRIVATE src/umd_uapi.c)

target_include_directories(
    umd-uapi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(umd-uapi PRIVATE c_std_11)
set_target_properties(
    umd-uapi
    PROPERTIES
        POSITION_INDEPENDENT_CODE
            ON
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        umd-uapi
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES api/umd-uapi/umd_uapi.h api/umd-uapi/pci_ids.h
    )
endif()
