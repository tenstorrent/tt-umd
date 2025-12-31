// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "assembly_programs_for_tests.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;

namespace test_utils {

inline void safe_test_cluster_start(Cluster* cluster) {
    auto architecture = cluster->get_chip(0)->get_tt_device()->get_arch();
    std::array<uint32_t, 12> brisc_program_default{};
    std::copy(
        brisc_configuration_program_default.cbegin(),
        brisc_configuration_program_default.cend(),
        std::next(brisc_program_default.begin(), 1));

    switch (architecture) {
        case tt::ARCH::WORMHOLE_B0:
            brisc_program_default[0] = WORMHOLE_BRISC_BASE_INSTRUCTION;
            break;
        case tt::ARCH::BLACKHOLE:
            brisc_program_default[0] = BLACKHOLE_BRISC_BASE_INSTRUCTION;
            break;
        default:
            return;
    }

    for (auto& chip_id : cluster->get_target_device_ids()) {
        for (const CoreCoord& tensix_core : cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
            auto chip = cluster->get_chip(chip_id);
            auto core = cluster->get_soc_descriptor(chip_id).translate_coord_to(tensix_core, CoordSystem::TRANSLATED);

            cluster->assert_risc_reset(chip_id, core, RiscType::ALL_TENSIX);

            cluster->l1_membar(chip_id, {core});

            cluster->write_to_device(
                brisc_program_default.data(), brisc_program_default.size() * sizeof(std::uint32_t), chip_id, core, 0);

            cluster->l1_membar(chip_id, {core});

            cluster->deassert_risc_reset(chip_id, core, RiscType::BRISC);

            cluster->l1_membar(chip_id, {core});

            cluster->assert_risc_reset(chip_id, core, RiscType::ALL_TENSIX);

            cluster->l1_membar(chip_id, {core});
        }
    }

    cluster->start_device({});
}

}  // namespace test_utils
