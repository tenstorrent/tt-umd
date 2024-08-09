
set(UNIT_TESTS_BH_SRCS
    test_silicon_driver_bh.cpp
    ../test_utils/generate_cluster_desc.cpp
)

add_executable(unit_tests_blackhole ${UNIT_TESTS_BH_SRCS})
target_link_libraries(unit_tests_blackhole PRIVATE test_common)
set_target_properties(unit_tests_blackhole PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/umd/blackhole)

add_custom_target(umd_unit_tests DEPENDS unit_tests_blackhole)