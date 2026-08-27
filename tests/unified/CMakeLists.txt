set(UNIFIED_TESTS_SRCS
    multiprocess.cpp
    test_tlb.cpp
)

add_executable(unified_tests ${UNIFIED_TESTS_SRCS})
target_link_libraries(unified_tests PRIVATE test_common)

# Optional: enables TestClusterExportDmabufLoopback's real RDMA READ over the exported dma-buf.
# Without libibverbs-dev, that test is still registered but GTEST_SKIPs at runtime.
find_library(IBVERBS_LIBRARY NAMES ibverbs)
find_path(IBVERBS_INCLUDE_DIR NAMES infiniband/verbs.h)
if(IBVERBS_LIBRARY AND IBVERBS_INCLUDE_DIR)
    target_compile_definitions(unified_tests PRIVATE UMD_HAS_IBVERBS)
    target_include_directories(unified_tests PRIVATE ${IBVERBS_INCLUDE_DIR})
    target_link_libraries(unified_tests PRIVATE ${IBVERBS_LIBRARY})
endif()

set_target_properties(
    unified_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/unified
        OUTPUT_NAME
            unified_tests
)
