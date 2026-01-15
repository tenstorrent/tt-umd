add_executable(tt_topology_discovery_example tt_topology_discovery_example.cpp)

target_link_libraries(
    tt_topology_discovery_example
    PRIVATE
        umd::device
        tt-logger::tt-logger
)

set_target_properties(
    tt_topology_discovery_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/tt_topology_discovery_example/
        OUTPUT_NAME
            tt_topology_discovery_example
)
