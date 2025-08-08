// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "assembly_programs_for_tests.hpp"
#include "umd/device/cluster.h"

using namespace tt::umd;

namespace test_utils {

void setup_risc_cores_on_cluster(Cluster* cluster) {
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

    for (const CoreCoord& tensix_core : cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        auto chip = cluster->get_chip(0);
        auto core = cluster->get_soc_descriptor(0).translate_coord_to(tensix_core, CoordSystem::VIRTUAL);

        TensixSoftResetOptions brisc_core{TensixSoftResetOptions::BRISC};

        TensixSoftResetOptions risc_cores{TensixSoftResetOptions::NCRISC | ALL_TRISC_SOFT_RESET};

        chip->set_tensix_risc_reset(core, TENSIX_ASSERT_SOFT_RESET);

        cluster->l1_membar(0, {core});

        cluster->write_to_device(
            brisc_program_default.data(), brisc_program_default.size() * sizeof(std::uint32_t), 0, core, 0);

        cluster->l1_membar(0, {core});

        chip->unset_tensix_risc_reset(core, brisc_core);

        cluster->l1_membar(0, {core});

        chip->unset_tensix_risc_reset(core, risc_cores);
    }
}

}  // namespace test_utils
