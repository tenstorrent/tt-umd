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

    void check_noc_id_cores(ChipId chip, CoreType core_type, CoordSystem noc) {
        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_cores(core_type, noc);
        for (const CoreCoord& core : cores) {
            const auto [x, y] = read_noc_id_reg(chip, core, get_noc_index(noc));
            EXPECT_EQ(core.x, x);
            EXPECT_EQ(core.y, y);
        }
    }

    void check_noc_id_harvested_cores(ChipId chip, CoreType core_type, CoordSystem noc) {
        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_harvested_cores(core_type, noc);
        for (const CoreCoord& core : cores) {
            const auto [x, y] = read_noc_id_reg(chip, core, get_noc_index(noc));
            EXPECT_EQ(core.x, x);
            EXPECT_EQ(core.y, y);
        }
    }

    tt::ARCH get_chip_arch(ChipId chip) { return cluster_->get_cluster_description()->get_arch(chip); }

    Cluster* get_cluster() { return cluster_.get(); };

private:
    std::unique_ptr<Cluster> cluster_;

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

    static uint8_t get_noc_index(CoordSystem noc) { return (noc == CoordSystem::NOC0) ? 0 : 1; }
};

TEST_F(TestNoc, TestNoc0NodeId) {
    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::TENSIX, CoordSystem::NOC0);
        check_noc_id_harvested_cores(chip, CoreType::TENSIX, CoordSystem::NOC0);

        check_noc_id_cores(chip, CoreType::ETH, CoordSystem::NOC0);
        check_noc_id_harvested_cores(chip, CoreType::ETH, CoordSystem::NOC0);

        if (get_chip_arch(chip) == tt::ARCH::BLACKHOLE) {
            check_noc_id_cores(chip, CoreType::DRAM, CoordSystem::NOC0);
            check_noc_id_harvested_cores(chip, CoreType::DRAM, CoordSystem::NOC0);
        }

        check_noc_id_cores(chip, CoreType::ARC, CoordSystem::NOC0);

        check_noc_id_cores(chip, CoreType::PCIE, CoordSystem::NOC0);
        check_noc_id_harvested_cores(chip, CoreType::PCIE, CoordSystem::NOC0);

        check_noc_id_cores(chip, CoreType::SECURITY, CoordSystem::NOC0);

        check_noc_id_cores(chip, CoreType::L2CPU, CoordSystem::NOC0);

        check_noc_id_cores(chip, CoreType::ROUTER_ONLY, CoordSystem::NOC0);
    }
}

TEST_F(TestNoc, TestNoc1NodeId) {
    NocIdSwitcher noc1_switcher(NocId::NOC1);

    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::TENSIX, CoordSystem::NOC1);
        check_noc_id_harvested_cores(chip, CoreType::TENSIX, CoordSystem::NOC1);

        check_noc_id_cores(chip, CoreType::ETH, CoordSystem::NOC1);
        if (get_chip_arch(chip) != tt::ARCH::BLACKHOLE) {
            check_noc_id_harvested_cores(chip, CoreType::ETH, CoordSystem::NOC1);
        }

        if (get_chip_arch(chip) != tt::ARCH::WORMHOLE_B0) {
            check_noc_id_cores(chip, CoreType::DRAM, CoordSystem::NOC1);
            check_noc_id_harvested_cores(chip, CoreType::DRAM, CoordSystem::NOC1);
        }

        check_noc_id_cores(chip, CoreType::ARC, CoordSystem::NOC1);

        check_noc_id_cores(chip, CoreType::PCIE, CoordSystem::NOC1);

        // TODO: translated coordinate for harvested PCIE is not same on NOC0 and NOC1.
        // This needs to be fixed in some way in order for this to work on Blackhole
        // with enabled translation.
        if (get_chip_arch(chip) != tt::ARCH::BLACKHOLE) {
            check_noc_id_harvested_cores(chip, CoreType::PCIE, CoordSystem::NOC1);
        }

        check_noc_id_cores(chip, CoreType::SECURITY, CoordSystem::NOC1);

        check_noc_id_cores(chip, CoreType::L2CPU, CoordSystem::NOC1);

        if (get_chip_arch(chip) != tt::ARCH::BLACKHOLE) {
            check_noc_id_cores(chip, CoreType::ROUTER_ONLY, CoordSystem::NOC1);
        }
    }
}
