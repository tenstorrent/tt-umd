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
# would collide with UMD's. We therefore never add_subdirectory() it. Ways to
# consume it:
#   1. Preferred: an exported chippy CMake package (find_package(chippy) ->
#      chippy::grendel). Used automatically when available.
#   2. Prebuilt: CHIPPY_SOURCE_DIR (repo root) + CHIPPY_BUILD_DIR (lib/ CMake
#      build tree). Imports the .a files and headers; does not fetch or rebuild
#      chippy. Use this to build chippy once on a GCC >= 13 machine (e.g. NFS).
#   3. Interim: fetch the pinned source and build it in isolation via
#      ExternalProject, then import the resulting static libraries.
#
# NOTE: the prebuilt and ExternalProject import (library set, build-tree paths,
# include dirs) tracks chippy's internal build-tree layout at the pinned commit,
# so it breaks whenever chippy moves a target between directories or flips one
# between STATIC and INTERFACE. It is meant to be superseded by
# find_package(chippy) once chippy exports a package.

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

set(CHIPPY_SOURCE_DIR "" CACHE PATH "chippy repo root (directory that contains lib/)")
set(CHIPPY_BUILD_DIR "" CACHE PATH "chippy lib/ CMake build directory containing the grendel static libraries")

# Static libraries produced by chippy's lib/ build that the Grendel path needs:
# the link closure of chippy's `grendel` target. Build-tree paths mirror the lib/
# subdir structure, except transport_interface, which is declared in
# transport/CMakeLists.txt and so lands directly in transport/.
#
# chippy's arch_asic, mock_transport, smc_remap_transport and common targets are
# INTERFACE libraries (headers only) and produce no archive, so they contribute
# include dirs (below) but nothing here. lz4 is only a lib/test_framework
# dependency and is not in the Grendel closure.
set(_chippy_lib_relpaths
    arch/grendel/libgrendel.a
    register_map/grendel/quasar/libquasar_map.a
    register_map/grendel/mimir/libmimir_map.a
    register_map/grendel/keraunos/libkeraunos_map.a
    transport/jtag2axi_transport/jtag2axi_v2_transport/libjtag2axi_v2_transport.a
    transport/jtag2axi_transport/jtag2axi_v1_transport/libjtag2axi_v1_transport.a
    transport/jtag2axi_transport/jtag2axi_transport_interface/libjtag2axi_transport_interface.a
    transport/emu_axi_transport/libemu_axi_transport.a
    transport/sim_axi_transport/libsim_axi_transport.a
    transport/distsim_axi_transport/libdistsim_axi_transport.a
    transport/libtransport_interface.a
    address_translation/libaddress_translation.a
    common/logging/liblogging.a
    common/utils/libutils.a
)

function(umd_import_chippy_grendel chippy_lib_src chippy_build)
    # Include roots for the Grendel path: the source dirs chippy's grendel targets
    # export, including the header-only (INTERFACE) ones that ship no archive. chippy
    # uses flat includes ("asic.h", "noc_utils.h", ...), so every directory holding a
    # header reachable from lib/arch/grendel/*.h has to be listed.
    #
    # Not listed: spdlog, which chippy's common/logging/logging.h includes (reached via
    # arch/grendel/ip/quasar_smn.h). UMD's own spdlog headers satisfy it, since tt-umd
    # already links spdlog. Note chippy compiles liblogging.a against its own
    # header-only spdlog, so its logging types are ODR-sensitive to that version skew;
    # UMD code should not include chippy's logging.h if it can avoid it.
    set(_chippy_includes
        ${chippy_lib_src}/transport/transport_interface
        ${chippy_lib_src}/transport/transport_utils
        ${chippy_lib_src}/address_translation
        ${chippy_lib_src}/arch
        ${chippy_lib_src}/arch/asic
        ${chippy_lib_src}/arch/grendel
        ${chippy_lib_src}/transport/jtag2axi_transport/jtag2axi_transport_interface
        ${chippy_lib_src}/transport/emu_axi_transport
        ${chippy_lib_src}/transport/sim_axi_transport
        ${chippy_lib_src}/transport/distsim_axi_transport
        ${chippy_lib_src}/transport/mock_transport
        ${chippy_lib_src}/transport/smc_remap_transport
        ${chippy_lib_src}/common
        ${chippy_lib_src}/common/logging
        ${chippy_lib_src}/common/utils
        ${chippy_lib_src}/register_map/grendel/quasar/include
        ${chippy_lib_src}/register_map/grendel/mimir/include
        ${chippy_lib_src}/register_map/grendel/keraunos/include
    )
    set(_chippy_libs ${_chippy_lib_relpaths})
    list(TRANSFORM _chippy_libs PREPEND "${chippy_build}/")

    add_library(umd_grendel_jtag INTERFACE)
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
    set(_chippy_libs ${_chippy_libs} PARENT_SCOPE)
endfunction()

# 2. Prebuilt: import a chippy lib/ build that already contains the grendel archives.
if(CHIPPY_BUILD_DIR)
    if(CHIPPY_SOURCE_DIR STREQUAL "")
        message(
            FATAL_ERROR
            "CHIPPY_BUILD_DIR is set but CHIPPY_SOURCE_DIR is empty. "
            "CHIPPY_SOURCE_DIR must be the chippy repo root (the directory that contains lib/)."
        )
    endif()
    get_filename_component(_chippy_source "${CHIPPY_SOURCE_DIR}" ABSOLUTE)
    get_filename_component(_chippy_build "${CHIPPY_BUILD_DIR}" ABSOLUTE)
    set(_chippy_lib_src "${_chippy_source}/lib")
    if(NOT EXISTS "${_chippy_lib_src}/CMakeLists.txt")
        message(
            FATAL_ERROR
            "CHIPPY_SOURCE_DIR=${_chippy_source} does not contain lib/CMakeLists.txt."
        )
    endif()
    umd_import_chippy_grendel("${_chippy_lib_src}" "${_chippy_build}")
    foreach(_chippy_lib IN LISTS _chippy_libs)
        if(NOT EXISTS "${_chippy_lib}")
            message(
                FATAL_ERROR
                "Prebuilt chippy is missing ${_chippy_lib}. "
                "On a GCC >= 13 host, from the chippy checkout at CHIPPY_SOURCE_DIR:\n"
                "  cmake -S lib -B <CHIPPY_BUILD_DIR> -DCMAKE_BUILD_TYPE=Release "
                "-DCHIPPY_BUILD_UNIT_TESTS=OFF -DBUILD_EXCLUDE_JLINK=TRUE "
                "-DCMAKE_POSITION_INDEPENDENT_CODE=ON\n"
                "  cmake --build <CHIPPY_BUILD_DIR> --target grendel"
            )
        endif()
    endforeach()
    message(STATUS "Grendel JTAG: using prebuilt chippy from ${_chippy_build}")
    return()
endif()

# 3. Interim: fetch pinned source + isolated ExternalProject build.
message(STATUS "Grendel JTAG: chippy package not found; using interim isolated ExternalProject build")

set(CHIPPY_GIT_REPOSITORY
    "https://yyz-gitlab.local.tenstorrent.com/syseng-platform/chippy.git"
    CACHE STRING
    "chippy git repository"
)
set(CHIPPY_GIT_TAG "bdcc120458fe181d1593cc08e69a8d1bbee14e6d" CACHE STRING "chippy pinned commit")

include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)
include(ExternalProject)

# Fetch source only. Never add_subdirectory chippy (see header comment).
#
# lfs.fetchexclude: chippy LFS-tracks files under validation/ and .gitlab/, none of
# which lib/ needs. Without this the checkout fails outright wherever git-lfs is
# installed but cannot reach the LFS endpoint (containers, CI).
CPMAddPackage(
    NAME chippy
    GIT_REPOSITORY ${CHIPPY_GIT_REPOSITORY}
    GIT_TAG ${CHIPPY_GIT_TAG}
    GIT_CONFIG
    lfs.fetchexclude=*
    DOWNLOAD_ONLY YES
)

set(_chippy_lib_src "${chippy_SOURCE_DIR}/lib")
set(_chippy_build "${CMAKE_CURRENT_BINARY_DIR}/chippy-build")
set(_chippy_libs ${_chippy_lib_relpaths})
list(TRANSFORM _chippy_libs PREPEND "${_chippy_build}/")

ExternalProject_Add(
    chippy_ext
    SOURCE_DIR "${_chippy_lib_src}"
    BINARY_DIR "${_chippy_build}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release -DCHIPPY_BUILD_UNIT_TESTS=OFF -DBUILD_EXCLUDE_JLINK=TRUE
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    # Build only the grendel target and its dependencies. chippy's `all` also
    # builds the Blackhole/Wormhole register maps and lib/test_framework, which
    # fetches yaml-cpp and CLI11 from GitHub.
    BUILD_COMMAND
        ${CMAKE_COMMAND} --build ${_chippy_build} --target grendel
    INSTALL_COMMAND
        "" # chippy exports no install rules; we import build-tree static libs
    BUILD_BYPRODUCTS
        ${_chippy_libs}
)

umd_import_chippy_grendel("${_chippy_lib_src}" "${_chippy_build}")
add_dependencies(umd_grendel_jtag chippy_ext)

# NOTE: C++20 is NOT applied here. It is set per-source on the single Grendel
# protocol translation unit (Issue 2) so UMD's public standard stays C++17.
