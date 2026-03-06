# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Auto-discover tests
file(GLOB TEST_SOURCES *.c)

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(test-${test_name} ${test_src})
    set_target_properties(test-${test_name} PROPERTIES OUTPUT_NAME ${test_name})
    target_link_libraries(test-${test_name} PRIVATE ttdal)
    add_test(NAME ${test_name} COMMAND test-${test_name})
endforeach()
