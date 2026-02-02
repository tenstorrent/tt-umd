// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <cstdint>

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

    void verify_noc_id_cores_via_other_noc(ChipId chip, CoreType core_type, CoordSystem this_noc) {
        CoordSystem other_noc = (this_noc == CoordSystem::NOC0) ? CoordSystem::NOC1 : CoordSystem::NOC0;

        // Set NOC context to this_noc for consistent read operations.
        NocIdSwitcher this_noc_switcher(static_cast<NocId>(get_noc_index(this_noc)));

        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_cores(core_type, this_noc);

        for (const CoreCoord& core : cores) {
            {
                // Read NOC_NODE_ID register from hardware via this_noc.
                const auto [this_x, this_y] = read_noc_id_reg(chip, core, get_noc_index(this_noc));
                CoreCoord this_noc_coord(this_x, this_y, core_type, this_noc);

                // Switch context to other_noc to read its perspective of the same core.
                // Note: The switcher has automatic storage, hence after the current scope the destructor
                // sets the NOC to it's previous value which here is this_noc.
                NocIdSwitcher other_noc_switcher(static_cast<NocId>(get_noc_index(other_noc)));

                // Read NOC_NODE_ID register from hardware via other_noc.
                // Note: The NOC via which we read must match the noc_index parameter because some registers
                // return NOC-dependent values (coordinates relative to the active NOC).
                const auto [other_x, other_y] = read_noc_id_reg(chip, core, get_noc_index(other_noc));
                CoreCoord other_noc_coord(other_x, other_y, core_type, other_noc);

                // Verify host-side coordinate translation by converting this_noc coordinates
                // to other_noc coordinates and comparing against hardware-reported values.
                auto other_noc_coord_soc_desc =
                    cluster_->get_soc_descriptor(chip).translate_coord_to(this_noc_coord, other_noc);

                EXPECT_EQ(other_noc_coord.x, other_noc_coord_soc_desc.x);
                EXPECT_EQ(other_noc_coord.y, other_noc_coord_soc_desc.y);
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
        std::string core_name;
        switch (core_type) {
            case CoreType::TENSIX:
                core_name = "TENSIX";
                break;
            case CoreType::ETH:
                core_name = "ETH";
                break;
            case CoreType::DRAM:
                core_name = "DRAM";
                break;
            case CoreType::ARC:
                core_name = "ARC";
                break;
            case CoreType::PCIE:
                core_name = "PCIE";
                break;
            case CoreType::SECURITY:
                core_name = "SECURITY";
                break;
            case CoreType::L2CPU:
                core_name = "L2CPU";
                break;
            case CoreType::ROUTER_ONLY:
                core_name = "ROUTER_ONLY";
                break;
            default:
                core_name = "UNKNOWN";
                break;
        }
        std::string noc_name = (noc == CoordSystem::NOC0) ? "NOC0" : "NOC1";
        return core_name + "_" + noc_name;
    });
