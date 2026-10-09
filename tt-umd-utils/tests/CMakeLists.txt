set(TT_UMD_UTILS_TESTS_SRCS
    test_semver.cpp
    test_errors.cpp
    test_robust_mutex.cpp
    test_op_timeout_guard.cpp
)

add_executable(tt_umd_utils_tests ${TT_UMD_UTILS_TESTS_SRCS})
target_link_libraries(
    tt_umd_utils_tests
    PRIVATE
        tt-umd-utils::tt-umd-utils
        gtest_main
        gtest
        pthread
)
set_target_properties(
    tt_umd_utils_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/utils
        OUTPUT_NAME
            tt_umd_utils_tests
)
