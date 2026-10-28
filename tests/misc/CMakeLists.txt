set(UMD_MISC_TESTS_SRCS
    test_kmd_mutex.cpp
    test_lock_manager.cpp
    test_wait_progress_logger.cpp
    test_poll_until.cpp
    test_enum_array.cpp
    test_cpp_compat.cpp
    test_warm_reset_completion.cpp
    test_open_device_or_skip.cpp
)

add_executable(umd_misc_tests ${UMD_MISC_TESTS_SRCS})
target_link_libraries(umd_misc_tests PRIVATE test_common)
set_target_properties(
    umd_misc_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/misc
        OUTPUT_NAME
            umd_misc_tests
)
