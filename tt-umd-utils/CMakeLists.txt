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

# Helpers shared by the UMD components but not part of their API: visible inside this build only and never installed.
# Header-only, so any target can use them without linking a second copy of tt-umd-utils.
add_library(tt-umd-utils-internal INTERFACE)
add_library(tt-umd-utils::internal ALIAS tt-umd-utils-internal)
target_include_directories(
    tt-umd-utils-internal
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>
)
target_link_libraries(
    tt-umd-utils-internal
    INTERFACE
        fmt::fmt-header-only
        tt-logger::tt-logger
)

# Tracy profiling for the code built into tt-umd. Kept apart from tt-umd-utils::internal, which the tests link too: they
# would otherwise get a second copy of the Tracy client next to the one inside tt-umd.
add_library(tt-umd-utils-tracy INTERFACE)
add_library(tt-umd-utils::tracy ALIAS tt-umd-utils-tracy)
target_link_libraries(tt-umd-utils-tracy INTERFACE tt-umd-utils::internal)
if(TT_UMD_ENABLE_TRACY)
    target_link_libraries(tt-umd-utils-tracy INTERFACE TracyClient)
    target_compile_definitions(tt-umd-utils-tracy INTERFACE TRACY_ENABLE)
endif()

# Not installed as a package of its own: it ships inside tt-umd, whose headers include these.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT umd-dev)
endif()

if(TT_UMD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
