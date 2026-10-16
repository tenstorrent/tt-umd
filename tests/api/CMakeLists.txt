set(API_TESTS_SRCS
    test_cluster_descriptor.cpp
    test_soc_descriptor.cpp
    test_device_memcpy.cpp
    test_sysmem_manager.cpp
    test_simulation_sysmem_manager.cpp
    test_sim_multichip.cpp
    test_frame_pointer.cpp
    test_tt_device.cpp
    test_noc.cpp
    test_jtag.cpp
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

add_executable(api_tests ${API_TESTS_SRCS})
target_link_libraries(api_tests PRIVATE test_common)
set_target_properties(
    api_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/api
        OUTPUT_NAME
            api_tests
)
