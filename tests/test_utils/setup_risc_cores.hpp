// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>

#include "assembly_programs_for_tests.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;

namespace test_utils {

inline void safe_test_cluster_start(Cluster* cluster) {
    static std::mutex mtx;
    {
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

        std::lock_guard<std::mutex> lock(mtx);

        for (auto& chip_id : cluster->get_target_device_ids()) {
            auto tensix_cores =
                cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
            std::unordered_set<CoreCoord> all_tensix_cores_translated{tensix_cores.begin(), tensix_cores.end()};

            for (const CoreCoord& tensix_core_translated : all_tensix_cores_translated) {
                cluster->assert_risc_reset(chip_id, tensix_core_translated, RiscType::ALL_TENSIX);
            }

            cluster->l1_membar(chip_id, all_tensix_cores_translated);

            for (const CoreCoord& tensix_core_translated : all_tensix_cores_translated) {
                cluster->write_to_device(
                    brisc_program_default.data(),
                    brisc_program_default.size() * sizeof(std::uint32_t),
                    chip_id,
                    tensix_core_translated,
                    0);
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
    }

    cluster->start_device({});
}

}  // namespace test_utils
