set(BAREMETAL_TESTS_SRCS
    test_cluster_descriptor_offline.cpp
    test_core_coord_translation_wh.cpp
    test_core_coord_translation_bh.cpp
    test_soc_descriptor.cpp
    test_long_jump.cpp
    test_notification_mechanism.cpp
)

add_executable(baremetal_tests ${BAREMETAL_TESTS_SRCS})
target_link_libraries(baremetal_tests PRIVATE test_common)
set_target_properties(
    baremetal_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/baremetal
        OUTPUT_NAME
            baremetal_tests
)
