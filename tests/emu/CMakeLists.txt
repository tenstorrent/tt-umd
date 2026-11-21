add_executable(emu_tests test_emu_tt_device.cpp)
target_link_libraries(
    emu_tests
    PRIVATE
        test_common
        umd_grendel_jtag
)
# The chippy headers need C++20; the test reaches the device through the internal test-access header.
target_include_directories(
    emu_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/tt-umd/src/tt_device
        ${PROJECT_SOURCE_DIR}/tt-umd/src/tt_device/protocol
)
target_compile_options(emu_tests PRIVATE -std=c++20)

set_target_properties(
    emu_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/emu
        OUTPUT_NAME
            emu_tests
)
