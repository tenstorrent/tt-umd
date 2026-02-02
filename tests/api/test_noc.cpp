// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <cstdint>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt;
using namespace tt::umd;

class TestNoc : public ::testing::Test {
public:
    void SetUp() override {
        cluster_ = std::make_unique<Cluster>();
        if (cluster_->get_cluster_description()->get_all_chips().empty()) {
            GTEST_SKIP() << "No chips present on the system. Skipping test.";
        }
    }

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

    void verify_noc_id_cores_via_other_noc(ChipId chip, CoreType core_type, CoordSystem this_noc) {
        CoordSystem other_noc = (this_noc == CoordSystem::NOC0) ? CoordSystem::NOC1 : CoordSystem::NOC0;

        // Set NOC context to this_noc for consistent read operations.
        NocIdSwitcher this_noc_switcher(static_cast<NocId>(get_noc_index(this_noc)));
        std::cout << "Current NOC is NOC:" << static_cast<uint32_t>(get_noc_index(this_noc)) << "\n";

        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_cores(core_type, this_noc);

        for (const CoreCoord& core : cores) {
            {
                // Read via this_noc the coordinate of the other_noc for the current core.
                const auto [other_x, other_y] = read_noc_id_reg(chip, core, get_noc_index(other_noc));

                // Represent these coords in the system from which their regs were read.
                CoreCoord other_noc_coord(other_x, other_y, core_type, other_noc);

                // Translate the current core (which is represented in this_noc) to the other_noc.
                auto other_noc_coord_soc_desc = cluster_->get_soc_descriptor(chip).translate_coord_to(core, other_noc);

                EXPECT_EQ(other_noc_coord.x, other_noc_coord_soc_desc.x)
                    << " on NOC" << static_cast<uint32_t>(get_noc_index(other_noc));
                EXPECT_EQ(other_noc_coord.y, other_noc_coord_soc_desc.y)
                    << " on NOC" << static_cast<uint32_t>(get_noc_index(other_noc));
            }
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

class TestNocValidity : public TestNoc, public ::testing::WithParamInterface<std::tuple<CoreType, CoordSystem>> {};

TEST_P(TestNocValidity, VerifyNocTranslation) {
    auto [core_type, noc] = GetParam();

    if (get_chip_arch(0) == ARCH::BLACKHOLE) {
        if (core_type == CoreType::ROUTER_ONLY) {
            GTEST_SKIP() << "Mapping on device side does not correlate correctly to the mapping on host side";
        }
        if (core_type == CoreType::PCIE || core_type == CoreType::ARC || core_type == CoreType::SECURITY ||
            core_type == CoreType::L2CPU || core_type == CoreType::ROUTER_ONLY) {
            GTEST_SKIP() << "Skipping test for core type: " << to_str(core_type);
        }
    }

    if (get_chip_arch(0) == ARCH::WORMHOLE_B0) {
        if (core_type == CoreType::PCIE || core_type == CoreType::ARC || core_type == CoreType::ROUTER_ONLY) {
            GTEST_SKIP() << "Skipping test for core type: " << to_str(core_type);
        }
    }

    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        verify_noc_id_cores_via_other_noc(chip, core_type, noc);
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllCoreTypesAndNocs,
    TestNocValidity,
    ::testing::Combine(
        ::testing::Values(
            CoreType::TENSIX,
            CoreType::ETH,
            CoreType::DRAM,
            CoreType::ARC,
            CoreType::PCIE,
            CoreType::SECURITY,
            CoreType::L2CPU,
            CoreType::ROUTER_ONLY),
        ::testing::Values(CoordSystem::NOC0, CoordSystem::NOC1)),
    [](const ::testing::TestParamInfo<std::tuple<CoreType, CoordSystem>>& info) {
        CoreType core_type = std::get<0>(info.param);
        CoordSystem noc = std::get<1>(info.param);
        return to_str(core_type) + "_" + to_str(noc);
    });
