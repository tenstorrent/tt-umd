add_executable(listener_example listener_example.cpp)

target_link_libraries(listener_example PRIVATE umd::device)

target_include_directories(listener_example PRIVATE ${TTLOGGER_INCLUDES})

add_executable(notifier_example notifier_example.cpp)

target_link_libraries(notifier_example PRIVATE umd::device)

target_include_directories(notifier_example PRIVATE ${TTLOGGER_INCLUDES})

set_target_properties(
    listener_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/reset_notification_example/
        OUTPUT_NAME
            listener_example
)

set_target_properties(
    notifier_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/reset_notification_example/
        OUTPUT_NAME
            notifier_example
)
