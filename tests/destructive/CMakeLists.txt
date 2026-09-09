set(DESTRUCTIVE_TESTS_SRCS
    test_hang_detection.cpp
    test_warm_reset_after_noc_hang.cpp
)

add_executable(destructive_tests ${DESTRUCTIVE_TESTS_SRCS})
target_link_libraries(destructive_tests PRIVATE test_common)
set_target_properties(
    destructive_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/destructive
        OUTPUT_NAME
            destructive_tests
)
