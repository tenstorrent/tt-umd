# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# General purpose utilities which are not specific to Tenstorrent hardware, shared by the UMD components.
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

set_target_properties(
    tt-umd-utils
    PROPERTIES
        POSITION_INDEPENDENT_CODE
            ON
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-umd-utils
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES
                api/tt-umd-utils/error.hpp
                api/tt-umd-utils/error_detail.hpp
                api/tt-umd-utils/mutex_interface.hpp
                api/tt-umd-utils/op_timeout_guard.hpp
                api/tt-umd-utils/robust_mutex.hpp
                api/tt-umd-utils/semver.hpp
    )
endif()

# tt-umd links this library privately and is the one which is installed, but its public headers include the
# headers of this library, so they are installed alongside the tt-umd headers.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT umd-dev)
endif()

if(TT_UMD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
