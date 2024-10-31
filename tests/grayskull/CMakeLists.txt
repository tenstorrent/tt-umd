set(UNIT_TESTS_GS_SRCS test_silicon_driver.cpp)

add_executable(unit_tests_grayskull ${UNIT_TESTS_GS_SRCS})
target_link_libraries(unit_tests_grayskull PRIVATE test_common)
set_target_properties(
    unit_tests_grayskull
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/grayskull
        OUTPUT_NAME
            unit_tests
)

add_custom_target(umd_unit_tests DEPENDS unit_tests_grayskull)
