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
)
# tt-umd's INCLUDE_DIRECTORIES carries its private third-party deps (e.g. asio), which are
# system includes on the library itself. Re-expose them as SYSTEM so clang-tidy keeps skipping
# third-party headers; without SYSTEM the copy lands as plain -I and trips warnings-as-errors.
target_include_directories(test_common SYSTEM INTERFACE "$<TARGET_PROPERTY:umd::tt-umd,INCLUDE_DIRECTORIES>")
target_compile_definitions(test_common INTERFACE UMD_TESTS_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}")

if(MASTER_PROJECT)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/microbenchmark)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/baremetal)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hang_detection)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/blackhole)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/misc)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/pcie)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/unified)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/wormhole)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/galaxy)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test_utils)

add_custom_target(
    umd_tests
    DEPENDS
        unit_tests_blackhole
        unit_tests_wormhole
        unit_tests_glx
        test_pcie_device
        api_tests
        hang_detection_tests
        baremetal_tests
        umd_misc_tests
        unified_tests
        test_utils_tests
)
