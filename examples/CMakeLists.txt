add_executable(tt_device_example tt_device_example.cpp)

target_link_libraries(
    tt_device_example
    PRIVATE
        umd::Common
        umd::device
        umd::Firmware
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        tt-logger::tt-logger
)

set_target_properties(
    tt_device_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/example/
        OUTPUT_NAME
            tt_device_example
)
