# Shadow the cache variable with a blank value
# Placing a no-op .clang-tidy file at the root of CPM cache is insufficient as some projects may define
# their own .clang-tidy within themselves and still not be clean against it <cough>flatbuffers</cough>
set(CMAKE_C_CLANG_TIDY "")
set(CMAKE_CXX_CLANG_TIDY "")

# Disable all warnings in third-party code
# This keeps build output clean and prevents failures from dependencies we don't control
add_compile_options(-w)

include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)

include(${PROJECT_SOURCE_DIR}/cmake/ttexalens_private.cmake)

####################################################################################################################
# google test
####################################################################################################################
if(TT_UMD_BUILD_TESTS)
    CPMAddPackage(
        NAME googletest
        GITHUB_REPOSITORY google/googletest
        GIT_TAG v1.13.0
        VERSION 1.13.0
        SYSTEM YES
        OPTIONS
            "INSTALL_GTEST OFF"
    )
    if(googletest_ADDED)
        target_compile_options(gtest PRIVATE -Wno-implicit-int-float-conversion)
    endif()
endif()

####################################################################################################################
# yaml-cpp
####################################################################################################################
CPMAddPackage(
    NAME yaml-cpp
    GITHUB_REPOSITORY jbeder/yaml-cpp
    GIT_TAG
        2f86d13775d119edbb69af52e5f566fd65c6953b # 0.8.0 + patches
    SYSTEM YES
    OPTIONS
        "YAML_CPP_BUILD_TESTS OFF"
        "YAML_CPP_BUILD_TOOLS OFF"
        "YAML_BUILD_SHARED_LIBS OFF"
)

if(yaml-cpp_ADDED)
    set_target_properties(
        yaml-cpp
        PROPERTIES
            DEBUG_POSTFIX
                ""
    )
endif()

###################################################################################################################
# Nanomsg
###################################################################################################################
if(TT_UMD_BUILD_SIMULATION)
    CPMAddPackage(
        NAME nanomsg
        GITHUB_REPOSITORY nanomsg/nng
        GIT_TAG v1.8.0
        SYSTEM YES
        OPTIONS
            "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
            "BUILD_SHARED_LIBS OFF"
            "NNG_TESTS OFF"
            "NNG_TOOLS OFF"
    )
endif()

###################################################################################################################
# Flatbuffers
###################################################################################################################
if(TT_UMD_BUILD_SIMULATION)
    CPMAddPackage(
        NAME flatbuffers
        GITHUB_REPOSITORY google/flatbuffers
        GIT_TAG v24.3.25
        SYSTEM YES
        OPTIONS
            "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
            "FLATBUFFERS_BUILD_FLATC ON"
            "FLATBUFFERS_BUILD_TESTS OFF"
            "FLATBUFFERS_SKIP_MONSTER_EXTRA ON"
            "FLATBUFFERS_STRICT_MODE ON"
    )

    if(NOT TARGET flatbuffers::flatbuffers)
        message(
            INFO
            "CPM Added flatbuffers, creating ALIAS flatbuffers::flatbuffers"
        )
        add_library(flatbuffers::flatbuffers ALIAS flatbuffers)
    endif()
endif()

###################################################################################################################
# libuv (for process management)
###################################################################################################################
if(TT_UMD_BUILD_SIMULATION)
    CPMAddPackage(
        NAME libuv
        GITHUB_REPOSITORY libuv/libuv
        GIT_TAG v1.51.0
        SYSTEM YES
        OPTIONS
            "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
            "LIBUV_BUILD_TESTS OFF"
            "LIBUV_BUILD_SHARED OFF"
            "BUILD_SHARED_LIBS OFF"
    )
endif()

###################################################################################################################
# fmt : https://github.com/fmtlib/fmt
###################################################################################################################

CPMAddPackage(NAME fmt GITHUB_REPOSITORY fmtlib/fmt GIT_TAG 11.1.4 SYSTEM YES)

###################################################################################################################
# nanobench (for uBenchmarking)
###################################################################################################################
if(MASTER_PROJECT)
    CPMAddPackage(NAME nanobench GITHUB_REPOSITORY martinus/nanobench GIT_TAG v4.3.11 SYSTEM YES)
endif()

####################################################################################################################
# spdlog
####################################################################################################################
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.15.2
    SYSTEM YES
    OPTIONS
        "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
        "SPDLOG_FMT_EXTERNAL_HO ON"
        "SPDLOG_INSTALL ON"
)

####################################################################################################################
# tt-logger
####################################################################################################################
CPMAddPackage(NAME tt-logger GITHUB_REPOSITORY tenstorrent/tt-logger VERSION 1.1.7 SYSTEM YES)

####################################################################################################################
# cxxopts
####################################################################################################################
CPMAddPackage(
    NAME cxxopts
    GITHUB_REPOSITORY jarro2783/cxxopts
    GIT_TAG
        dbf4c6a66816f6c3872b46cc6af119ad227e04e1 #version 3.2.1 + patches
    SYSTEM YES
    OPTIONS
        "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
)

####################################################################################################################
# nanobind
####################################################################################################################
CPMAddPackage(
    NAME nanobind
    GITHUB_REPOSITORY wjakob/nanobind
    VERSION 2.10.2
    SYSTEM YES
    OPTIONS
        "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
)


####################################################################################################################
# picosha2
####################################################################################################################
CPMAddPackage(
    NAME picosha2
    GITHUB_REPOSITORY okdshin/PicoSHA2
    GIT_TAG v1.0.1
    SYSTEM YES
    OPTIONS
        "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
    DOWNLOAD_ONLY YES
)

if(picosha2_ADDED)
    # picosha2 is header-only, create an interface library
    add_library(picosha2 INTERFACE)
    target_include_directories(picosha2 SYSTEM INTERFACE ${picosha2_SOURCE_DIR})
    add_library(picosha2::picosha2 ALIAS picosha2)
endif()

####################################################################################################################
# asio (Standalone)
####################################################################################################################
CPMAddPackage(NAME umd_asio GITHUB_REPOSITORY chriskohlhoff/asio GIT_TAG asio-1-30-2 SYSTEM YES DOWNLOAD_ONLY YES)

if(umd_asio_ADDED)
    add_library(umd_asio_lib INTERFACE)

    target_include_directories(umd_asio_lib SYSTEM INTERFACE "${umd_asio_SOURCE_DIR}/asio/include")

    # Attach the STANDALONE flag only to umd asio library
    target_compile_definitions(umd_asio_lib INTERFACE ASIO_STANDALONE)
    target_compile_definitions(umd_asio_lib INTERFACE ASIO_NO_DEPRECATED)
endif()
