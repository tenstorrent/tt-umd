add_executable(
    mock_main
    main.cpp
    mock_device_model.cpp
)
target_link_libraries(mock_main PRIVATE tt_device)
