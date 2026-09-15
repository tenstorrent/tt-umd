# RDMA dma-buf loopback test. Reached only when TT_UMD_BUILD_RDMA is ON; libibverbs is located and
# version-checked once in cmake/ibverbs.cmake.
add_executable(
    rdma_tests
    rdma_loopback.cpp
    test_dmabuf_loopback.cpp
)
target_link_libraries(
    rdma_tests
    PRIVATE
        test_common
        ibverbs::ibverbs
)

set_target_properties(
    rdma_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/rdma
        OUTPUT_NAME
            rdma_tests
)
