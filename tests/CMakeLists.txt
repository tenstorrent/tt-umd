add_library(test_common INTERFACE)
target_link_libraries(
    test_common
    INTERFACE
        umd::tt-umd
        umd::Firmware
        gtest_main
        gtest
        pthread
        spdlog::spdlog_header_only
        fmt::fmt-header-only
        tt-logger::tt-logger
        $<$<BOOL:${TT_UMD_BUILD_SIMULATION}>:nng>
)
target_include_directories(
    test_common
    INTERFACE
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        "$<TARGET_PROPERTY:umd::tt-umd,INCLUDE_DIRECTORIES>"
)
target_compile_definitions(test_common INTERFACE UMD_TESTS_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}")

# EXPERIMENT: precompiled header for the test executables, propagated to every test target
# via this test_common INTERFACE library (gtest + the heavy externals/STL that essentially
# every test TU includes). Gated on clang-tidy OFF for the same reason as the tt-umd library
# PCH: clang-tidy (always clang) can't read a gcc-generated .gch, and build-device compiles
# the microbenchmark tests with gcc + clang-tidy ON. Language-scoped to C++ via a file path.
if(NOT TT_UMD_ENABLE_CLANG_TIDY)
    target_precompile_headers(test_common INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp>")
endif()

if(MASTER_PROJECT)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/microbenchmark)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/baremetal)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/blackhole)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/misc)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/pcie)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/unified)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/wormhole)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/galaxy)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test_utils)

if(TT_UMD_BUILD_SIMULATION)
    message(STATUS "Building ${PROJECT_NAME} with Simulation Tests")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/simulation)
endif()

add_custom_target(
    umd_tests
    DEPENDS
        unit_tests_blackhole
        unit_tests_wormhole
        unit_tests_glx
        test_pcie_device
        api_tests
        baremetal_tests
        umd_misc_tests
        unified_tests
        test_utils_tests
)

if(TT_UMD_BUILD_SIMULATION)
    add_dependencies(umd_tests simulation_tests)
endif()
