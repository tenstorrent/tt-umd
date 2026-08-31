# Grendel (chippy) JTAG support.
#
# Defines the `umd_grendel_jtag` target that the Grendel JTAG device code links
# against. Access-gated behind TT_UMD_BUILD_GRENDEL_JTAG (OFF by default): with
# the option OFF, `umd_grendel_jtag` is an empty INTERFACE library, so a normal
# public build (no chippy access, no GCC >= 13) is completely unaffected. chippy
# lives on the internal GitLab; only users with access can fetch it.
#
# chippy is a C++20 / GCC >= 13 / CMake >= 3.30 codebase whose lib/CMakeLists.txt
# force-selects a compiler and defines dependency targets (yaml-cpp, lz4) that
# would collide with UMD's. We therefore never add_subdirectory() it. Two
# supported ways to consume it:
#   1. Preferred: an exported chippy CMake package (find_package(chippy) ->
#      chippy::grendel). Used automatically when available.
#   2. Interim: fetch the pinned source and build it in isolation via
#      ExternalProject (its toolchain and bundled deps stay inside its own CMake
#      run), then import the resulting static libraries.
#
# NOTE: the interim ExternalProject import below (library set, build-tree paths,
# include dirs) is authored against chippy's source layout but has NOT been
# build-verified in this environment (no GCC >= 13 available). It must be
# finalized against a real chippy build, or superseded by find_package(chippy)
# once the package lands.

if(NOT TT_UMD_BUILD_GRENDEL_JTAG)
    add_library(umd_grendel_jtag INTERFACE)
    add_library(umd::grendel_jtag ALIAS umd_grendel_jtag)
    return()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(
        FATAL_ERROR
        "TT_UMD_BUILD_GRENDEL_JTAG=ON requires GCC >= 13 (chippy is C++20). Found ${CMAKE_CXX_COMPILER_VERSION}."
    )
endif()

# 1. Preferred: exported chippy CMake package.
find_package(chippy CONFIG QUIET)
if(chippy_FOUND)
    add_library(umd_grendel_jtag INTERFACE)
    target_link_libraries(umd_grendel_jtag INTERFACE chippy::grendel)
    add_library(umd::grendel_jtag ALIAS umd_grendel_jtag)
    message(STATUS "Grendel JTAG: using exported chippy package (chippy::grendel)")
    return()
endif()

# 2. Interim: fetch pinned source + isolated ExternalProject build.
message(STATUS "Grendel JTAG: chippy package not found; using interim isolated ExternalProject build")

set(CHIPPY_GIT_REPOSITORY
    "https://yyz-gitlab.local.tenstorrent.com/syseng-platform/chippy.git"
    CACHE STRING "chippy git repository"
)
set(CHIPPY_GIT_TAG
    "bdcc120458fe181d1593cc08e69a8d1bbee14e6d"
    CACHE STRING "chippy pinned commit"
)

include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)
include(ExternalProject)

# Fetch source only. Never add_subdirectory chippy (see header comment).
CPMAddPackage(
    NAME chippy
    GIT_REPOSITORY ${CHIPPY_GIT_REPOSITORY}
    GIT_TAG ${CHIPPY_GIT_TAG}
    DOWNLOAD_ONLY YES
)

set(_chippy_lib_src "${chippy_SOURCE_DIR}/lib")
set(_chippy_build "${CMAKE_CURRENT_BINARY_DIR}/chippy-build")

# Static libraries produced by chippy's lib/ build that the Grendel path needs.
# Build-tree paths mirror the lib/ subdir structure. PROVISIONAL — confirm the
# set, names, and paths against a real GCC >= 13 chippy build.
set(_chippy_libs
    arch/grendel/libgrendel.a
    arch/libarch_asic.a
    register_map/grendel/quasar/libquasar_map.a
    register_map/grendel/mimir/libmimir_map.a
    register_map/grendel/keraunos/libkeraunos_map.a
    transport/jtag2axi_transport/jtag2axi_v2_transport/libjtag2axi_v2_transport.a
    transport/jtag2axi_transport/jtag2axi_v1_transport/libjtag2axi_v1_transport.a
    transport/jtag2axi_transport/jtag2axi_transport_interface/libjtag2axi_transport_interface.a
    transport/emu_axi_transport/libemu_axi_transport.a
    transport/mock_transport/libmock_transport.a
    transport/smc_remap_transport/libsmc_remap_transport.a
    transport/transport_interface/libtransport_interface.a
    address_translation/libaddress_translation.a
    common/libcommon.a
    common/logging/liblogging.a
    common/utils/libutils.a
    lz4/liblz4.a
)
list(TRANSFORM _chippy_libs PREPEND "${_chippy_build}/")

ExternalProject_Add(
    chippy_ext
    SOURCE_DIR "${_chippy_lib_src}"
    BINARY_DIR "${_chippy_build}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCHIPPY_BUILD_UNIT_TESTS=OFF
        -DBUILD_EXCLUDE_JLINK=TRUE
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    INSTALL_COMMAND "" # chippy exports no install rules; we import build-tree static libs
    BUILD_BYPRODUCTS ${_chippy_libs}
)

# Include roots for the Grendel path. PROVISIONAL — confirm against a real build.
set(_chippy_includes
    ${_chippy_lib_src}/transport/transport_interface
    ${_chippy_lib_src}/address_translation
    ${_chippy_lib_src}/arch
    ${_chippy_lib_src}/arch/grendel
    ${_chippy_lib_src}/transport/jtag2axi_transport/jtag2axi_transport_interface
    ${_chippy_lib_src}/transport/emu_axi_transport
    ${_chippy_lib_src}/transport/mock_transport
    ${_chippy_lib_src}/common
    ${_chippy_lib_src}/common/logging
    ${_chippy_lib_src}/register_map/grendel/quasar/include
    ${_chippy_lib_src}/register_map/grendel/mimir/include
    ${_chippy_lib_src}/register_map/grendel/keraunos/include
)

add_library(umd_grendel_jtag INTERFACE)
add_dependencies(umd_grendel_jtag chippy_ext)
# SYSTEM so chippy's headers don't trip UMD's warnings/clang-tidy.
target_include_directories(umd_grendel_jtag SYSTEM INTERFACE ${_chippy_includes})
# --start-group/--end-group: chippy's static libs are mutually referential and
# exported with no CMake link-order metadata, so let the linker resolve order.
target_link_libraries(
    umd_grendel_jtag
    INTERFACE
        -Wl,--start-group
        ${_chippy_libs}
        -Wl,--end-group
)
add_library(umd::grendel_jtag ALIAS umd_grendel_jtag)

# NOTE: C++20 is NOT applied here. It is set per-source on the single Grendel
# protocol translation unit (Issue 2) so UMD's public standard stays C++17.
