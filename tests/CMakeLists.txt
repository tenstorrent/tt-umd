# Tests currently depend on ARCH_NAME for compile time include paths
if(NOT DEFINED ENV{ARCH_NAME})
    message(
        FATAL_ERROR
        "Please set ARCH_NAME to grayskull, wormhole_b0, or blackhole"
    )
endif(NOT DEFINED ENV{ARCH_NAME})

add_library(test_common INTERFACE)
target_link_libraries(
    test_common
    INTERFACE
        umd_device
        gtest_main
        gtest
        pthread
        fmt::fmt-header-only
)
target_include_directories(
    test_common
    INTERFACE
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)
if($ENV{ARCH_NAME} STREQUAL "grayskull")
    message(STATUS "UMD: Building for Grayskull")
    target_include_directories(
        test_common
        INTERFACE
            ${PROJECT_SOURCE_DIR}/device/grayskull
            ${PROJECT_SOURCE_DIR}/src/firmware/riscv/grayskull
    )
elseif($ENV{ARCH_NAME} STREQUAL "wormhole_b0")
    message(STATUS "UMD: Building for Wormhole")
    target_include_directories(
        test_common
        INTERFACE
            ${PROJECT_SOURCE_DIR}/device/wormhole
            ${PROJECT_SOURCE_DIR}/src/firmware/riscv/wormhole
    )
elseif($ENV{ARCH_NAME} STREQUAL "blackhole")
    message(STATUS "UMD: Building for Blackhole")
    target_include_directories(
        test_common
        INTERFACE
            ${PROJECT_SOURCE_DIR}/device/blackhole
            ${PROJECT_SOURCE_DIR}/src/firmware/riscv/blackhole
    )
endif()

if(MASTER_PROJECT)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/microbenchmark)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/pcie)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/simulation)
if($ENV{ARCH_NAME} STREQUAL "wormhole_b0")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/wormhole)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/galaxy)
    add_custom_target(
        umd_unit_tests
        DEPENDS
            unit_tests_wormhole
            unit_tests_glx
    )
else()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/$ENV{ARCH_NAME})
endif()

add_custom_target(
    umd_tests
    DEPENDS
        umd_unit_tests
        simulation_tests
        test_pcie_device
        api_tests
)
