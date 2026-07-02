set(HANG_DETECTION_TESTS_SRCS test_hang_detection.cpp)

add_executable(hang_detection_tests ${HANG_DETECTION_TESTS_SRCS})
target_link_libraries(hang_detection_tests PRIVATE test_common)
set_target_properties(
    hang_detection_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/hang_detection
        OUTPUT_NAME
            hang_detection_tests
)
