// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Chip specific API examples.

#include <fmt/xchar.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/cluster_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

// TODO: Move to test_chip.
TEST(ApiChipTest, SimpleAPIShowcase) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    ChipId chip_id = umd_cluster->get_cluster_description()->get_chips_with_mmio().begin()->first;

    // TODO: In future, will be accessed through Chip api.
    umd_cluster->get_pcie_base_addr_from_device(chip_id);
    umd_cluster->get_num_host_channels(chip_id);
}

// TODO: Re-enable once we debug why it doesn't work #362
// // This tests puts a specific core into reset and then deasserts it using default deassert value
// // It reads back the risc reset reg to validate
// TEST(ApiChipTest, DeassertRiscResetOnCore) {
//     std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

//     tt_cxy_pair chip_core_coord = get_tensix_chip_core_coord(umd_cluster);

//     umd_cluster->assert_risc_reset_at_core(chip_core_coord);
//     umd_cluster->l1_membar(chip_core_coord.chip);
//     umd_cluster->deassert_risc_reset_at_core(chip_core_coord);
//     umd_cluster->l1_membar(chip_core_coord.chip);

//     uint32_t soft_reset_reg_addr = 0xFFB121B0;
//     uint32_t expected_risc_reset_val = static_cast<uint32_t>(TENSIX_DEASSERT_SOFT_RESET);
//     uint32_t risc_reset_val;
//     umd_cluster->read_from_device(&risc_reset_val, chip_core_coord, soft_reset_reg_addr, sizeof(uint32_t),
//     "REG_TLB"); EXPECT_EQ(expected_risc_reset_val, risc_reset_val);
// }

// // This tests puts a specific core into reset and then specifies a legal deassert value
// // It reads back the risc reset reg to validate
// TEST(ApiChipTest, SpecifyLegalDeassertRiscResetOnCore) {
//     std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

//     tt_cxy_pair chip_core_coord = get_tensix_chip_core_coord(umd_cluster);

//     umd_cluster->assert_risc_reset_at_core(chip_core_coord);
//     TensixSoftResetOptions deassert_val = ALL_TRISC_SOFT_RESET | TensixSoftResetOptions::STAGGERED_START;
//     umd_cluster->deassert_risc_reset_at_core(chip_core_coord, deassert_val);
//     umd_cluster->l1_membar(chip_core_coord.chip);

//     uint32_t soft_reset_reg_addr = 0xFFB121B0;
//     uint32_t risc_reset_val;
//     umd_cluster->read_from_device(&risc_reset_val, chip_core_coord, soft_reset_reg_addr, sizeof(uint32_t),
//     "REG_TLB"); EXPECT_EQ(static_cast<uint32_t>(deassert_val), risc_reset_val);
// }

// // // This tests puts a specific core into reset and then specifies an illegal deassert value
// // // It reads back the risc reset reg to validate that reset reg is in a legal state
// TEST(ApiChipTest, SpecifyIllegalDeassertRiscResetOnCore) {
//     std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

//     tt_cxy_pair chip_core_coord = get_tensix_chip_core_coord(umd_cluster);

//     umd_cluster->assert_risc_reset_at_core(chip_core_coord);

//     TensixSoftResetOptions deassert_val = static_cast<TensixSoftResetOptions>(0xDEADBEEF);
//     umd_cluster->deassert_risc_reset_at_core(chip_core_coord, deassert_val);
//     umd_cluster->l1_membar(chip_core_coord.chip);

//     uint32_t soft_reset_reg_addr = 0xFFB121B0;
//     uint32_t risc_reset_val;
//     umd_cluster->read_from_device(&risc_reset_val, chip_core_coord, soft_reset_reg_addr, sizeof(uint32_t),
//     "REG_TLB"); uint32_t expected_deassert_val = static_cast<uint32_t>(deassert_val & ALL_TENSIX_SOFT_RESET);
//     EXPECT_EQ(risc_reset_val, expected_deassert_val);
// }
