
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
