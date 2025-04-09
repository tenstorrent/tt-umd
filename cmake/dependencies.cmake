function(fetch_dependencies)
    # Shadow the cache variable with a blank value
    # Placing a no-op .clang-tidy file at the root of CPM cache is insufficient as some projects may define
    # their own .clang-tidy within themselves and still not be clean against it <cough>flatbuffers</cough>
    set(CMAKE_C_CLANG_TIDY "")
    set(CMAKE_CXX_CLANG_TIDY "")

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
    CPMAddPackage(
        NAME Boost
        VERSION 1.86.0
        URL
            https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
            URL_HASH
            SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
        OPTIONS
            "BOOST_ENABLE_CMAKE ON"
            "BOOST_SKIP_INSTALL_RULES ON"
            "BUILD_SHARED_LIBS OFF"
            "BOOST_INCLUDE_LIBRARIES interprocess"
    )

    ###################################################################################################################
    # Nanomsg
    ###################################################################################################################
    CPMAddPackage(
        NAME nanomsg
        GITHUB_REPOSITORY nanomsg/nng
        GIT_TAG v1.8.0
        OPTIONS
            "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
            "BUILD_SHARED_LIBS OFF"
            "NNG_TESTS OFF"
            "NNG_TOOLS OFF"
    )

    ###################################################################################################################
    # libuv (for process management)
    ###################################################################################################################
    CPMAddPackage(
        NAME libuv
        GITHUB_REPOSITORY libuv/libuv
        GIT_TAG v1.48.0
        OPTIONS
            "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
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
    CPMAddPackage(
        NAME spdlog
        GITHUB_REPOSITORY gabime/spdlog
        GIT_TAG
            96a8f6250cbf4e8c76387c614f666710a2fa9bad # Version v 1.15+fmtlib fixes
        OPTIONS
            "CMAKE_MESSAGE_LOG_LEVEL NOTICE"
    )
endfunction()
fetch_dependencies()
