add_executable(listener_example listener_example.cpp)

target_link_libraries(
    listener_example
    PRIVATE
        umd::device
        tt-logger::tt-logger
)

add_executable(notifier_example notifier_example.cpp)

target_link_libraries(
    notifier_example
    PRIVATE
        umd::device
        tt-logger::tt-logger
)

set_target_properties(
    listener_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/pre_reset_notification/
        OUTPUT_NAME
            listener_example
)

set_target_properties(
    notifier_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/pre_reset_notification/
        OUTPUT_NAME
            notifier_example
)
