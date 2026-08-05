find_library(IBVERBS_LIBRARY NAMES ibverbs REQUIRED)

add_executable(dmabuf_p2p dmabuf_p2p.cpp)
target_link_libraries(
    dmabuf_p2p
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
        pthread
)

add_executable(dmabuf_loopback dmabuf_loopback.cpp)
target_link_libraries(
    dmabuf_loopback
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
        pthread
)

set_target_properties(
    dmabuf_p2p
    dmabuf_loopback
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/rdma_testing/
)
