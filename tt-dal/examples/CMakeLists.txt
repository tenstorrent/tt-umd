# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Auto-discover examples (each is a directory with src/main.c)
file(GLOB EXAMPLE_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

foreach(example_dir ${EXAMPLE_DIRS})
    set(example_src ${CMAKE_CURRENT_SOURCE_DIR}/${example_dir}/src/main.c)
    if(EXISTS ${example_src})
        add_executable(example-${example_dir} ${example_src})
        target_link_libraries(example-${example_dir} PRIVATE ttdal)
        set_target_properties(example-${example_dir} PROPERTIES OUTPUT_NAME ${example_dir})
    endif()
endforeach()
