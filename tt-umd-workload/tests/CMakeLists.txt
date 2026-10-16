# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Workload tests are built into the test binaries defined under tests/, which CI runs by name.
target_sources(
    api_tests
    PRIVATE
        api/test_chip.cpp
        api/test_cluster.cpp
        api/test_cluster_options.cpp
        api/test_device_io.cpp
        api/test_risc_program.cpp
        api/test_tt_visible_devices.cpp
)
target_sources(baremetal_tests PRIVATE baremetal/test_cluster.cpp)
target_sources(unit_tests_blackhole PRIVATE blackhole/test_cluster_bh.cpp)
target_sources(unified_tests PRIVATE unified/multiprocess.cpp)
target_sources(
    unit_tests_wormhole
    PRIVATE
        wormhole/test_cluster_wh.cpp
        wormhole/test_umd_remote_api_stability.cpp
)

if(TARGET umd_microbenchmark)
    target_sources(umd_microbenchmark PRIVATE microbenchmark/benchmarks/open_cluster/test_open_cluster.cpp)
endif()

if(TT_UMD_BUILD_EMULE)
    target_sources(baremetal_tests PRIVATE baremetal/test_sw_emule_chip.cpp)
    target_link_libraries(baremetal_tests PRIVATE tt-emule::headers)
endif()

add_subdirectory(galaxy)
