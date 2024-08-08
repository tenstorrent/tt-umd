
add_library(test_common INTERFACE)
target_link_libraries(test_common INTERFACE umd_device gtest_main gtest pthread fmt::fmt-header-only)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/simulation)
add_custom_target(tests DEPENDS simulation_tests)
