// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "assembly_programs_for_tests.hpp"
#include "tt-umd-utils/robust_mutex.hpp"
#include "tt-umd-workload/cluster.hpp"

using namespace tt;
using namespace tt::umd;

namespace tt::umd::test_utils {

inline void safe_test_cluster_start(Cluster* cluster) {
    static RobustMutex mtx("safe_test_cluster_start");
    static std::once_flag init_flag;
    std::call_once(init_flag, [&]() { mtx.initialize(); });

    std::lock_guard<RobustMutex> lock(mtx);

    static constexpr uint32_t start_address_in_l1 = 0x20;
    std::array<uint32_t, 12> brisc_park_program{};
    switch (cluster->get_chip(0)->get_tt_device()->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            brisc_park_program = wh_brisc_park_program;
            break;
        case tt::ARCH::BLACKHOLE:
            brisc_park_program = bh_brisc_park_program;
            break;
        default:
            return;
    }

    for (auto& chip_id : cluster->get_target_device_ids()) {
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
        std::unordered_set<CoreCoord> all_tensix_cores_translated{tensix_cores.begin(), tensix_cores.end()};

        for (const CoreCoord& tensix_core_translated : all_tensix_cores_translated) {
            cluster->assert_risc_reset(chip_id, tensix_core_translated, RiscType::ALL_TENSIX);
        }

        cluster->l1_membar(chip_id, all_tensix_cores_translated);

        for (const CoreCoord& tensix_core_translated : all_tensix_cores_translated) {
            cluster->write_to_device(
                &BRISC_TRAMPOLINE_JMP, sizeof(BRISC_TRAMPOLINE_JMP), chip_id, tensix_core_translated, 0);
            cluster->write_to_device(
                brisc_park_program.data(),
                brisc_park_program.size() * sizeof(std::uint32_t),
                chip_id,
                tensix_core_translated,
                start_address_in_l1);
        }

        cluster->l1_membar(chip_id, all_tensix_cores_translated);

        for (const CoreCoord& tensix_core_translated : all_tensix_cores_translated) {
            cluster->deassert_risc_reset(chip_id, tensix_core_translated, RiscType::BRISC);
        }

        cluster->l1_membar(chip_id, all_tensix_cores_translated);

        for (const CoreCoord& tensix_core_translated : all_tensix_cores_translated) {
            cluster->assert_risc_reset(chip_id, tensix_core_translated, RiscType::ALL_TENSIX);
        }

        cluster->l1_membar(chip_id, all_tensix_cores_translated);
    }

    cluster->start_device({});
}

}  // namespace tt::umd::test_utils
