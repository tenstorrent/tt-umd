set(UNIT_TESTS_BH_SRCS
    test_arc_messages_bh.cpp
    test_chip_info_bh.cpp
    test_kmd_lib_events_bh.cpp
    test_device_event_reader_bh.cpp
)

add_executable(unit_tests_blackhole ${UNIT_TESTS_BH_SRCS})
target_link_libraries(unit_tests_blackhole PRIVATE test_common)
# tt-umd links tt-kmd-lib privately, so the events tests depend on it directly.
target_link_libraries(unit_tests_blackhole PRIVATE tt-kmd-lib::tt-kmd-lib)
set_target_properties(
    unit_tests_blackhole
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/blackhole
        OUTPUT_NAME
            unit_tests
)
