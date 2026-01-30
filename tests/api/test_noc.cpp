// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt;
using namespace tt::umd;

class TestNoc : public ::testing::Test {
public:
    void SetUp() override { cluster_ = std::make_unique<Cluster>(); }

    tt_xy_pair read_noc_id_reg(ChipId chip, CoreCoord core, uint8_t noc_index) {
        const uint64_t noc_node_id_reg_addr =
            cluster_->get_tt_device(0)->get_architecture_implementation()->get_noc_reg_base(core.core_type, noc_index) +
            cluster_->get_tt_device(0)->get_architecture_implementation()->get_noc_node_id_offset();
        uint32_t noc_node_id_val;
        cluster_->read_from_device_reg(&noc_node_id_val, chip, core, noc_node_id_reg_addr, sizeof(noc_node_id_val));
        uint32_t x = noc_node_id_val & 0x3F;
        uint32_t y = (noc_node_id_val >> 6) & 0x3F;
        return tt_xy_pair(x, y);
    }

    void check_noc_id_cores(ChipId chip, CoreType core_type, uint8_t noc_index) {
        CoordSystem coord_system = (noc_index == 0) ? CoordSystem::NOC0 : CoordSystem::NOC1;
        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_cores(core_type, coord_system);
        for (const CoreCoord& core : cores) {
            const auto [x, y] = read_noc_id_reg(chip, core, noc_index);
            EXPECT_EQ(core.x, x);
            EXPECT_EQ(core.y, y);
        }
    }

    void check_noc_id_harvested_cores(ChipId chip, CoreType core_type, uint8_t noc_index) {
        CoordSystem coord_system = (noc_index == 0) ? CoordSystem::NOC0 : CoordSystem::NOC1;
        const std::vector<CoreCoord>& cores =
            cluster_->get_soc_descriptor(chip).get_harvested_cores(core_type, coord_system);
        for (const CoreCoord& core : cores) {
            const auto [x, y] = read_noc_id_reg(chip, core, noc_index);
            EXPECT_EQ(core.x, x);
            EXPECT_EQ(core.y, y);
        }
    }

    tt::ARCH get_chip_arch(ChipId chip) { return cluster_->get_cluster_description()->get_arch(chip); }

    Cluster* get_cluster() { return cluster_.get(); };

private:
    std::unique_ptr<Cluster> cluster_;
};

TEST_F(TestNoc, TestNoc0NodeId) {
    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::TENSIX, 0);
        check_noc_id_harvested_cores(chip, CoreType::TENSIX, 0);

        check_noc_id_cores(chip, CoreType::ETH, 0);
        check_noc_id_harvested_cores(chip, CoreType::ETH, 0);

        if (get_chip_arch(chip) == tt::ARCH::BLACKHOLE) {
            check_noc_id_cores(chip, CoreType::DRAM, 0);
            check_noc_id_harvested_cores(chip, CoreType::DRAM, 0);
        }

        check_noc_id_cores(chip, CoreType::ARC, 0);

        check_noc_id_cores(chip, CoreType::PCIE, 0);
        check_noc_id_harvested_cores(chip, CoreType::PCIE, 0);

        check_noc_id_cores(chip, CoreType::SECURITY, 0);

        check_noc_id_cores(chip, CoreType::L2CPU, 0);

        check_noc_id_cores(chip, CoreType::ROUTER_ONLY, 0);
    }
}

TEST_F(TestNoc, TestNoc1NodeId) {
    NocIdSwitcher noc1_switcher(NocId::NOC1);

    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::TENSIX, 1);
        check_noc_id_harvested_cores(chip, CoreType::TENSIX, 1);

        check_noc_id_cores(chip, CoreType::ETH, 1);
        if (get_chip_arch(chip) != tt::ARCH::BLACKHOLE) {
            check_noc_id_harvested_cores(chip, CoreType::ETH, 1);
        }

        if (get_chip_arch(chip) != tt::ARCH::WORMHOLE_B0) {
            check_noc_id_cores(chip, CoreType::DRAM, 1);
            check_noc_id_harvested_cores(chip, CoreType::DRAM, 1);
        }

        check_noc_id_cores(chip, CoreType::ARC, 1);

        check_noc_id_cores(chip, CoreType::PCIE, 1);

        // TODO: translated coordinate for harvested PCIE is not same on NOC0 and NOC1.
        // This needs to be fixed in some way in order for this to work on Blackhole
        // with enabled translation.
        if (get_chip_arch(chip) != tt::ARCH::BLACKHOLE) {
            check_noc_id_harvested_cores(chip, CoreType::PCIE, 1);
        }

        check_noc_id_cores(chip, CoreType::SECURITY, 1);

        check_noc_id_cores(chip, CoreType::L2CPU, 1);

        if (get_chip_arch(chip) != tt::ARCH::BLACKHOLE) {
            check_noc_id_cores(chip, CoreType::ROUTER_ONLY, 1);
        }
    }
}
