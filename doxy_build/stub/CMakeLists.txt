add_executable(
    stub_main
    main.cpp
    stub_device_model.cpp
)
target_link_libraries(stub_main PRIVATE tt_device)
