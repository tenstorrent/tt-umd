add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/simulation)
add_custom_target(tests DEPENDS simulation_tests)
