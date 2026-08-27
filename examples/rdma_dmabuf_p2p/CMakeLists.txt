# This example drives real RDMA verbs, so it needs libibverbs. It is skipped rather than failing
# the configure step when libibverbs-dev is not installed, so enabling TT_UMD_BUILD_EXAMPLES stays
# safe on hosts without an RDMA stack.
find_library(IBVERBS_LIBRARY NAMES ibverbs)
if(NOT IBVERBS_LIBRARY)
    message(STATUS "rdma_dmabuf_p2p example: libibverbs not found, skipping")
    return()
endif()

add_executable(dmabuf_target dmabuf_target.cpp)

target_link_libraries(
    dmabuf_target
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
)

# The initiator only speaks RDMA against a plain host buffer, so it does not link UMD.
add_executable(dmabuf_initiator dmabuf_initiator.cpp)

target_link_libraries(dmabuf_initiator PRIVATE ${IBVERBS_LIBRARY})

set_target_properties(
    dmabuf_target
    dmabuf_initiator
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/rdma_dmabuf_p2p/
)
