set(API_TESTS_SRCS
    test_cluster_descriptor.cpp
    test_soc_descriptor.cpp
    test_dma_zero_copy_async.cpp
    test_grendel_device_io.cpp
    test_grendel_att_fold.cpp
    test_device_memcpy.cpp
    test_sysmem_manager.cpp
    test_simulation_sysmem_manager.cpp
    test_sim_multichip.cpp
    test_frame_pointer.cpp
    test_tt_device.cpp
    test_noc.cpp
    test_arc_telemetry.cpp
    test_firmware_info_provider.cpp
    test_warm_reset.cpp
)

if(TT_UMD_BUILD_SIMULATION)
    list(APPEND API_TESTS_SRCS test_ttsim_device_io.cpp)
    list(APPEND API_TESTS_SRCS test_simulation_connector.cpp)
    list(APPEND API_TESTS_SRCS test_simulation_server_protocol.cpp)
    list(APPEND API_TESTS_SRCS test_simulation_device_identity.cpp)
    list(APPEND API_TESTS_SRCS test_rtl_sim_ip_partition.cpp)
endif()

if(TT_UMD_BUILD_GRENDEL_JTAG)
    set(GRENDEL_CHIPPY_TEST_SRCS test_grendel_jtag_protocol.cpp test_grendel_flat_address.cpp)
    list(APPEND API_TESTS_SRCS ${GRENDEL_CHIPPY_TEST_SRCS})
endif()

add_executable(api_tests ${API_TESTS_SRCS})
target_link_libraries(api_tests PRIVATE test_common)
# tt-umd links tt-kmd-lib privately, so the Grendel DMA loopback test reaches its headers only
# because libtt-umd.so re-exports them. Depend on it directly instead.
target_link_libraries(api_tests PRIVATE tt-kmd-lib::tt-kmd-lib)

if(TT_UMD_BUILD_GRENDEL_JTAG)
    # The Grendel tests include chippy headers, which need C++20, and the internal
    # protocol and transport headers.
    target_link_libraries(api_tests PRIVATE umd_grendel_jtag)
    target_include_directories(
        api_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/tt-umd/src/tt_device
            ${PROJECT_SOURCE_DIR}/tt-umd/src/tt_device/protocol
    )
    set_source_files_properties(
        ${GRENDEL_CHIPPY_TEST_SRCS}
        PROPERTIES
            COMPILE_OPTIONS
                "-std=c++20"
    )
endif()
set_target_properties(
    api_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/api
        OUTPUT_NAME
            api_tests
)
