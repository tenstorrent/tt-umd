add_executable(tt_device_example tt_device_example.cpp)

target_link_libraries(
    tt_device_example
    PRIVATE
        umd::device
        tt-logger::tt-logger
)

set_target_properties(
    tt_device_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/tt_device_example/
        OUTPUT_NAME
            tt_device_example
)
