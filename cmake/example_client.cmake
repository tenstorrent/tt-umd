# This file is meant as an example CMakeLists.txt of how to build a client from the installed UMD artifacts.
# It also contains some default third party dependencies that might be needed by the client, but are needed for some UMD 'clients' such as tools and examples.
# You can copy some parts or whole CMake configuration from this file to your client's CMakeLists.txt file.

# We have to manually add all third_party dependencies here since we are building from install artifacts
include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)
CPMAddPackage(NAME fmt GITHUB_REPOSITORY fmtlib/fmt GIT_TAG 11.1.4)
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.15.2
    OPTIONS
        "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
        "SPDLOG_FMT_EXTERNAL_HO ON"
        "SPDLOG_INSTALL ON"
)
CPMAddPackage(NAME tt-logger GITHUB_REPOSITORY tenstorrent/tt-logger VERSION 1.1.7)
CPMAddPackage(
    NAME cxxopts
    GITHUB_REPOSITORY jarro2783/cxxopts
    GIT_TAG
        dbf4c6a66816f6c3872b46cc6af119ad227e04e1 #version 3.2.1 + patches
    OPTIONS
        "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
)
CPMAddPackage(NAME nanobind GITHUB_REPOSITORY wjakob/nanobind VERSION 2.10.2 OPTIONS "CMAKE_MESSAGE_LOG_LEVEL NOTICE")

# Find the installed UMD package
find_package(umd QUIET)
if(NOT umd_FOUND)
    message(
        FATAL_ERROR
        "UMD package not found! When building from install artifacts, you need to tell CMake where to find the installed UMD package.\n"
        "\n"
        "Solutions:\n"
        "  1. Use CMAKE_PREFIX_PATH:\n"
        "     cmake . -DCMAKE_PREFIX_PATH=/path/to/umd/install\n"
        "\n"
        "  2. Use umd_DIR directly:\n"
        "     cmake . -Dumd_DIR=/path/to/umd/install/lib/cmake/umd\n"
        "\n"
        "  3. Set environment variable:\n"
        "     export CMAKE_PREFIX_PATH=/path/to/umd/install\n"
        "\n"
        "The install path is where you ran: cmake --install <build-dir> --prefix <install-path>"
    )
endif()
