add_library(tools_common INTERFACE)
target_link_libraries(
    tools_common
    INTERFACE
        umd::device
        umd::Firmware
        fmt::fmt-header-only
)
target_include_directories(
    tools_common
    INTERFACE
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        "$<TARGET_PROPERTY:umd::device,INCLUDE_DIRECTORIES>"
)

add_executable(tool_topology topology.cpp)
target_link_libraries(tool_topology PRIVATE tools_common)
set_target_properties(
    tool_topology
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            topology
)

add_custom_target(umd_tools DEPENDS tool_topology)
