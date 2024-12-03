function(fetch_dependencies)
    # Shadow the cache variable with a blank value
    # Placing a no-op .clang-tidy file at the root of CPM cache is insufficient as some projects may define
    # their own .clang-tidy within themselves and still not be clean against it <cough>flatbuffers</cough>
    set(CMAKE_C_CLANG_TIDY "")
    set(CMAKE_CXX_CLANG_TIDY "")
    set(ENV{CPM_SOURCE_CACHE} "${PROJECT_SOURCE_DIR}/.cpmcache")

    include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)

    ####################################################################################################################
    # google test
    ####################################################################################################################
    CPMAddPackage(
        NAME googletest
        GITHUB_REPOSITORY google/googletest
        GIT_TAG v1.13.0
        VERSION 1.13.0
        OPTIONS
            "INSTALL_GTEST OFF"
    )

    ####################################################################################################################
    # yaml-cpp
    ####################################################################################################################
    CPMAddPackage(
        NAME yaml-cpp
        GITHUB_REPOSITORY jbeder/yaml-cpp
        GIT_TAG 0.8.0
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
    # boost::interprocess
    ###################################################################################################################
    include(${PROJECT_SOURCE_DIR}/cmake/fetch_boost.cmake)
    fetch_boost_library(interprocess)

    ###################################################################################################################
    # Nanomsg
    ###################################################################################################################
    CPMAddPackage(
        NAME nanomsg
        GITHUB_REPOSITORY nanomsg/nng
        GIT_TAG v1.8.0
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "NNG_TESTS OFF"
            "NNG_TOOLS OFF"
    )

    ###################################################################################################################
    # Flatbuffers
    ###################################################################################################################
    CPMAddPackage(
        NAME flatbuffers
        GITHUB_REPOSITORY google/flatbuffers
        GIT_TAG v24.3.25
        OPTIONS
            "FLATBUFFERS_BUILD_FLATC ON"
            "FLATBUFFERS_BUILD_TESTS OFF"
            "FLATBUFFERS_SKIP_MONSTER_EXTRA ON"
            "FLATBUFFERS_STRICT_MODE ON"
    )

    set(FLATC_EXE ${flatbuffers_BINARY_DIR}/flatc PARENT_SCOPE)

    ###################################################################################################################
    # libuv (for process management)
    ###################################################################################################################
    CPMAddPackage(
        NAME libuv
        GITHUB_REPOSITORY libuv/libuv
        GIT_TAG v1.48.0
        OPTIONS
            "LIBUV_BUILD_TESTS OFF"
            "LIBUV_BUILD_SHARED OFF"
    )

    ###################################################################################################################
    # fmt : https://github.com/fmtlib/fmt
    ###################################################################################################################

    CPMAddPackage(NAME fmt GITHUB_REPOSITORY fmtlib/fmt GIT_TAG 11.0.1)

    ###################################################################################################################
    # nanobench (for uBenchmarking)
    ###################################################################################################################
    if(MASTER_PROJECT)
        CPMAddPackage(NAME nanobench GITHUB_REPOSITORY martinus/nanobench GIT_TAG v4.3.11)
    endif()

    ####################################################################################################################
    # spdlog
    ####################################################################################################################
    CPMAddPackage(NAME spdlog GITHUB_REPOSITORY gabime/spdlog GIT_TAG v1.14.1 VERSION v1.14.1)
endfunction()
fetch_dependencies()
