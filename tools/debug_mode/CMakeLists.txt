add_executable(break_wh break_wh.cpp)
target_link_libraries(break_wh PRIVATE tools_common)
set_target_properties(
    break_wh
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            break_wh
)
