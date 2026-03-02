# Device tests organized by component

# Cluster tests
set(CLUSTER_TESTS_SRCS
    cluster/test_cluster.cpp
    cluster/test_device_io.cpp
    cluster/test_noc.cpp
    cluster/test_risc_program.cpp
    cluster/test_software_harvesting.cpp
    cluster/test_tt_visible_devices.cpp
    cluster/test_warm_reset.cpp
    cluster/test_tlb_manager.cpp
)

# Chip tests
set(CHIP_TESTS_SRCS
    chip/test_chip.cpp
)

# SoC descriptor tests
set(SOC_DESCRIPTOR_TESTS_SRCS
    soc_descriptor/test_soc_descriptor.cpp
)

# TT Device tests
set(TT_DEVICE_TESTS_SRCS
    tt_device/test_tt_device.cpp
    tt_device/test_spi_tt_device.cpp
)

# Sysmem manager tests
set(SYSMEM_MANAGER_TESTS_SRCS
    sysmem_manager/test_sysmem_manager.cpp
    sysmem_manager/test_simulation_sysmem_manager.cpp
)

# JTAG tests
set(JTAG_TESTS_SRCS
    jtag/test_jtag.cpp
)

# ARC telemetry tests
set(ARC_TELEMETRY_TESTS_SRCS
    arc_telemetry/test_arc_telemetry.cpp
)

# Cluster descriptor tests
set(CLUSTER_DESCRIPTOR_TESTS_SRCS
    cluster_descriptor/test_cluster_descriptor.cpp
)

# Combine all device tests
set(DEVICE_TESTS_SRCS
    ${CLUSTER_TESTS_SRCS}
    ${CHIP_TESTS_SRCS}
    ${SOC_DESCRIPTOR_TESTS_SRCS}
    ${TT_DEVICE_TESTS_SRCS}
    ${SYSMEM_MANAGER_TESTS_SRCS}
    ${JTAG_TESTS_SRCS}
    ${ARC_TELEMETRY_TESTS_SRCS}
    ${CLUSTER_DESCRIPTOR_TESTS_SRCS}
)

add_executable(device_tests ${DEVICE_TESTS_SRCS})
target_link_libraries(device_tests PRIVATE test_common)
set_target_properties(
    device_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/device
        OUTPUT_NAME
            device_tests
)
