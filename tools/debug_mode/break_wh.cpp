// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <cxxopts.hpp>
#include <iomanip>
#include <sstream>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/utils/debug_mode.hpp"
using namespace tt::umd;

int main(int argc, char* argv[]) {
    tt::umd::DebugMode::enable();
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        return 0;
    }
    Chip* chip = cluster->get_chip(0);
    if (!chip) {
        return 1;
    }
    if (chip->get_tt_device()->get_arch() != tt::ARCH::WORMHOLE_B0) {
        return 0;
    }

    chip->assert_risc_reset(RiscType::ALL);
    std::vector<uint32_t> ebreak_instr_vector(0x3FFFF - 1, 0x00100073);
    CoreCoord eth_1 = chip->get_soc_descriptor().get_coord_at({1, 0}, CoordSystem::NOC0);
    chip->write_to_device(eth_1, ebreak_instr_vector.data(), 0x0, ebreak_instr_vector.size());
    chip->deassert_risc_reset(RiscType::ALL, false);

    std::unique_ptr<Cluster> cluster2 = std::make_unique<Cluster>();
}
