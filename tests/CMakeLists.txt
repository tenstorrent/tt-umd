# Tests currently depend on ARCH_NAME for compile time include paths
if(NOT DEFINED ENV{ARCH_NAME})
    message(FATAL_ERROR "Please set ARCH_NAME to grayskull, wormhole_b0, or blackhole")
endif(NOT DEFINED ENV{ARCH_NAME})

add_library(test_common INTERFACE)
target_link_libraries(
    test_common
    INTERFACE
        umd::device
        umd::Firmware
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

# Download noc_parameters.h for tests
# Generally avoid downloading files in cmake configure step
# It can slow down reconfigures
# Here we skip downloading repeatedly
set(ARCHS
    grayskull
    wormhole
    blackhole
)
foreach(ARCH IN LISTS ARCHS)
    set(HW_NOC_PARAMETERS_URL
        "https://raw.githubusercontent.com/tenstorrent/tt-metal/refs/heads/main/tt_metal/hw/inc/${ARCH}/noc/noc_parameters.h"
    )
    set(DESTINATION_DIR ${CMAKE_CURRENT_BINARY_DIR}/${ARCH}/noc)
    file(MAKE_DIRECTORY ${DESTINATION_DIR})
    get_filename_component(fileName ${HW_NOC_PARAMETERS_URL} NAME)
    set(filePath ${DESTINATION_DIR}/${fileName})
    if(NOT EXISTS ${filePath})
        file(
            DOWNLOAD
                ${HW_NOC_PARAMETERS_URL}
                ${filePath}
            SHOW_PROGRESS
            STATUS status
        )
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "Failed to download ${HW_NOC_PARAMETERS_URL}")
        endif()
    endif()
endforeach()
if($ENV{ARCH_NAME} STREQUAL "wormhole_b0")
    include_directories("${CMAKE_CURRENT_BINARY_DIR}/wormhole")
else()
    include_directories("${CMAKE_CURRENT_BINARY_DIR}/$ENV{ARCH_NAME}")
endif()

if(MASTER_PROJECT)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/microbenchmark)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/pcie)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/misc)
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
        umd_misc_tests
)
