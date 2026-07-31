find_library(IBVERBS_LIBRARY NAMES ibverbs REQUIRED)

add_executable(dmabuf_target dmabuf_target.cpp)
target_link_libraries(
    dmabuf_target
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
        pthread
)

add_executable(dmabuf_initiator dmabuf_initiator.cpp)
target_link_libraries(
    dmabuf_initiator
    PRIVATE
        ${IBVERBS_LIBRARY}
        pthread
)

# Second, separate test pair: reads device DRAM through the *same* TLB window that is exported as the
# dma-buf — first by this host's CPU, then by the peer's NIC. See window_read_target.cpp.
add_executable(window_read_target window_read_target.cpp)
target_link_libraries(
    window_read_target
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
        pthread
)

add_executable(window_read_initiator window_read_initiator.cpp)
target_link_libraries(
    window_read_initiator
    PRIVATE
        ${IBVERBS_LIBRARY}
        pthread
)

set_target_properties(
    dmabuf_target
    dmabuf_initiator
    window_read_target
    window_read_initiator
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/rdma_testing/
)
