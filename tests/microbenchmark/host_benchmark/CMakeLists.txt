# Host-side perf comparisons (no device required for most cases). Kept as its own executable,
# separate from umd_microbenchmark, because these are one-off comparisons rather than routine
# benchmarks.
add_executable(umd_locks_benchmark test_locks.cpp)
target_link_libraries(
    umd_locks_benchmark
    PRIVATE
        test_common
        nanobench
)
target_include_directories(
    umd_locks_benchmark
    PRIVATE
        ${nanobench_SOURCE_DIR}/src/include
        # Parent microbenchmark dir, so "common/microbenchmark_utils.hpp" resolves.
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
set_target_properties(
    umd_locks_benchmark
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/microbenchmark
)
