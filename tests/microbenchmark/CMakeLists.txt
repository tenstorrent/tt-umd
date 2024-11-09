set(UBENCH_SRC test_rw_tensix.cpp)
add_executable(ubench ${UBENCH_SRC})
target_link_libraries(
    ubench
    PRIVATE
        test_common
        nanobench
)
target_include_directories(ubench PRIVATE ${nanobench_SOURCE_DIR}/src/include)
set_target_properties(
    ubench
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/ubenchmarks
)
