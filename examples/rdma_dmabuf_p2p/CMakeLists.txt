# Reached only when TT_UMD_BUILD_RDMA is ON; libibverbs is located and version-checked once in
# cmake/ibverbs.cmake.
add_executable(dmabuf_target dmabuf_target.cpp)

target_link_libraries(
    dmabuf_target
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ibverbs::ibverbs
)

# The initiator only speaks RDMA against a plain host buffer, so it does not link UMD.
add_executable(dmabuf_initiator dmabuf_initiator.cpp)

target_link_libraries(dmabuf_initiator PRIVATE ibverbs::ibverbs)

set_target_properties(
    dmabuf_target
    dmabuf_initiator
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/rdma_dmabuf_p2p/
)
