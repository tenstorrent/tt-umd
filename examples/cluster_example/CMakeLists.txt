add_executable(cluster_example cluster_example.cpp)

target_link_libraries(
    cluster_example
    PRIVATE
        umd::Common
        umd::device
        umd::Firmware
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        tt-logger::tt-logger
)

set_target_properties(
    cluster_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/cluster_example/
        OUTPUT_NAME
            cluster_example
)
