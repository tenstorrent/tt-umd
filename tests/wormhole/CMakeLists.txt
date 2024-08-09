
set(UNIT_TESTS_WH_SRCS
    test_silicon_driver_wh.cpp
    test_umd_remote_api_stability.cpp
    ../test_utils/generate_cluster_desc.cpp
)

add_executable(unit_tests_wormhole ${UNIT_TESTS_WH_SRCS})
target_link_libraries(unit_tests_wormhole PRIVATE test_common)
set_target_properties(unit_tests_wormhole PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/umd/wormhole_b0
    OUTPUT_NAME unit_tests
)

add_custom_target(umd_unit_tests DEPENDS unit_tests_wormhole)