set(TEST_UTILS_TESTS_SRCS test_utils.cpp)

add_executable(test_utils_tests ${TEST_UTILS_TESTS_SRCS})
target_link_libraries(test_utils_tests PRIVATE test_common)
set_target_properties(
    test_utils_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/test_utils
        OUTPUT_NAME
            test_utils
)
