add_library(tools_common INTERFACE)
target_link_libraries(
    tools_common
    INTERFACE
        umd::Common
        umd::device
        umd::Firmware
        fmt::fmt-header-only
        spdlog::spdlog_header_only
)

add_executable(topology topology.cpp)
target_link_libraries(topology PRIVATE tools_common)
set_target_properties(
    topology
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            topology
)

add_custom_target(umd_tools DEPENDS topology)
