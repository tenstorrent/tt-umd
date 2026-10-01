add_executable(l2cpu_tests test_l2cpu_loader.cpp)
target_link_libraries(l2cpu_tests PRIVATE test_common)
set_target_properties(
    l2cpu_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/l2cpu
        OUTPUT_NAME
            l2cpu_tests
)
