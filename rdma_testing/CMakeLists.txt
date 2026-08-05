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

# No verbs here on purpose - this one is UMD-only, so it can isolate DRAM addressability from
# anything in the dma-buf / P2P path.
add_executable(dram_pio_probe dram_pio_probe.cpp)
target_link_libraries(
    dram_pio_probe
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
)

set_target_properties(
    dmabuf_p2p
    dmabuf_loopback
    dram_pio_probe
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/rdma_testing/
)
