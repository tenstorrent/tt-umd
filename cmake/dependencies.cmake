
set(ENV{CPM_SOURCE_CACHE} "${PROJECT_SOURCE_DIR}/.cpmcache")

include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)

############################################################################################################################
# google test
############################################################################################################################
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.13.0
    VERSION 1.13.0
    OPTIONS "INSTALL_GTEST OFF"
)

############################################################################################################################
# yaml-cpp
############################################################################################################################
CPMAddPackage(
  NAME yaml-cpp
  GITHUB_REPOSITORY jbeder/yaml-cpp
  GIT_TAG 0.8.0
  OPTIONS
    "YAML_CPP_BUILD_TESTS OFF"
    "YAML_CPP_BUILD_TOOLS OFF"
    "YAML_BUILD_SHARED_LIBS OFF"
)

if (yaml-cpp_ADDED)
    set_target_properties(yaml-cpp PROPERTIES DEBUG_POSTFIX "")
endif()

############################################################################################################################
# boost::interprocess
############################################################################################################################
include(${PROJECT_SOURCE_DIR}/cmake/fetch_boost.cmake)
fetch_boost_library(interprocess)

############################################################################################################################
# Nanomsg
############################################################################################################################
CPMAddPackage(
    NAME nanomsg
    GITHUB_REPOSITORY nanomsg/nng
    GIT_TAG v1.8.0
    OPTIONS
        "BUILD_SHARED_LIBS ON"
        "NNG_TESTS OFF"
        "NNG_TOOLS OFF"
)

############################################################################################################################
# Flatbuffers
############################################################################################################################
CPMAddPackage(
    NAME flatbuffers
    GITHUB_REPOSITORY google/flatbuffers
    GIT_TAG v24.3.25
    OPTIONS
        "FLATBUFFERS_BUILD_FLATC OFF"
        "FLATBUFFERS_BUILD_TESTS OFF"
        "FLATBUFFERS_INSTALL OFF"
        "FLATBUFFERS_BUILD_FLATLIB OFF"
        "FLATBUFFERS_SKIP_MONSTER_EXTRA ON"
        "FLATBUFFERS_STRICT_MODE ON"
)

############################################################################################################################
# libuv (for process management)
############################################################################################################################
CPMAddPackage(
    NAME libuv
    GITHUB_REPOSITORY libuv/libuv
    GIT_TAG v1.48.0
    OPTIONS
        "LIBUV_BUILD_TESTS OFF"
)

############################################################################################################################
# fmt : https://github.com/fmtlib/fmt
############################################################################################################################

CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 11.0.1
)

# QUESTIONABLE ?????
if(NOT MASTER_PROJECT)
    set(nng_include_dir ${nanomsg_SOURCE_DIR}/include PARENT_SCOPE)
    set(flatbuffers_include_dir ${flatbuffers_SOURCE_DIR}/include PARENT_SCOPE)
    set(libuv_include_dir ${libuv_SOURCE_DIR}/include PARENT_SCOPE)
endif()

############################################################################################################################
# nanobench (for uBenchmarking)
############################################################################################################################
if (MASTER_PROJECT)
    CPMAddPackage(
        NAME nanobench
        GITHUB_REPOSITORY martinus/nanobench
        GIT_TAG v4.3.11
    )
endif()
