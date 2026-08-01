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

add_executable(dmabuf_p2p dmabuf_p2p.cpp)
target_link_libraries(
    dmabuf_p2p
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
        pthread
)

set_target_properties(
    dmabuf_target
    dmabuf_initiator
    dmabuf_p2p
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/rdma_testing/
)
