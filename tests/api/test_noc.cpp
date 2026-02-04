// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <cstdint>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

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

        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_cores(core_type, this_noc);

        for (const CoreCoord& core : cores) {
            // Read via this_noc the coordinate of the other_noc for the current core.
            const auto [other_x, other_y] = read_noc_id_reg(chip, core, get_noc_index(other_noc));

            // Represent the read coords in the system from which their regs were read.
            CoreCoord other_noc_coord(other_x, other_y, core_type, other_noc);

            // Translate the current from host-side core (which is represented in this_noc) to the other_noc.
            auto other_noc_coord_soc_desc = cluster_->get_soc_descriptor(chip).translate_coord_to(core, other_noc);

            EXPECT_EQ(other_noc_coord.x, other_noc_coord_soc_desc.x);
            EXPECT_EQ(other_noc_coord.y, other_noc_coord_soc_desc.y);
        }
    }

    void verify_noc_ids_differ_by_noc(ChipId chip, CoreType core_type, CoordSystem this_noc) {
        CoordSystem other_noc = (this_noc == CoordSystem::NOC0) ? CoordSystem::NOC1 : CoordSystem::NOC0;
        const std::vector<CoreCoord>& cores = cluster_->get_soc_descriptor(chip).get_cores(core_type, this_noc);

        for (const CoreCoord& core_this_noc : cores) {
            tt_xy_pair other_noc_reg_value_via_this_noc;
            tt_xy_pair other_noc_reg_value_via_other_noc;
            {
                NocIdSwitcher noc_switcher(static_cast<NocId>(get_noc_index(this_noc)));
                // Read via this_noc the coordinate of the other_noc (from the NODE_ID reg) for the current core.
                other_noc_reg_value_via_this_noc = read_noc_id_reg(chip, core_this_noc, get_noc_index(other_noc));
            }
            {
                NocIdSwitcher noc_switcher(static_cast<NocId>(get_noc_index(other_noc)));
                // Read via this_noc the coordinate of the other_noc (from the NODE_ID reg) for the current core.
                other_noc_reg_value_via_other_noc = read_noc_id_reg(chip, core_this_noc, get_noc_index(other_noc));
            }

            // We expect different values from the same NODE_ID register address because the returned
            // value depends on which NOC was used to perform the transaction.
            // NOTE: This verifies NOC0 and NOC1 coordinates are never the same. This holds true
            // because our grids have even dimensions. For an odd x odd grid, the center tile
            // would have identical coordinates in both NOC systems, causing this assertion to fail.
            EXPECT_NE(other_noc_reg_value_via_this_noc, other_noc_reg_value_via_other_noc);

            // Reading the other NOC's register via this NOC returns this NOC's coordinates,
            // since the NOC used for the transaction determines the coordinate space.
            EXPECT_EQ(other_noc_reg_value_via_this_noc, core_this_noc);

            auto core_other_noc = cluster_->get_soc_descriptor(chip).translate_coord_to(core_this_noc, other_noc);

            // Reading via the other NOC returns coordinates matching that NOC's coordinate space.
            EXPECT_EQ(other_noc_reg_value_via_other_noc, core_other_noc);
        }
    }

    Cluster* get_cluster() { return cluster_.get(); };

private:
    std::unique_ptr<Cluster> cluster_;

    tt_xy_pair read_noc_id_reg(ChipId chip, CoreCoord core, uint8_t noc_index) {
        auto noc_port = (core.core_type == CoreType::DRAM) ? get_dram_noc_port(core) : 0;
        // NOTE: The noc_port parameter is not used for Blackhole. Unlike Wormhole where DRAM banks
        // have multiple NOC ports with different register base addresses, Blackhole uses a single
        // register base address per core type.
        const uint64_t noc_node_id_reg_addr =
            cluster_->get_tt_device(0)->get_architecture_implementation()->get_noc_reg_base(
                core.core_type, noc_index, noc_port) +
            cluster_->get_tt_device(0)->get_architecture_implementation()->get_noc_node_id_offset();
        uint32_t noc_node_id_val;
        cluster_->read_from_device_reg(&noc_node_id_val, chip, core, noc_node_id_reg_addr, sizeof(noc_node_id_val));
        uint32_t x = noc_node_id_val & 0x3F;
        uint32_t y = (noc_node_id_val >> 6) & 0x3F;
        log_debug(
            tt::LogUMD,
            "Reading noc {} regs for chip {} core {} from addr {:x}. Result is raw {:x} which corresponds to core {}",
            noc_index,
            chip,
            core,
            noc_node_id_reg_addr,
            noc_node_id_val,
            tt_xy_pair(x, y));
        return tt_xy_pair(x, y);
    }

    static uint8_t get_noc_index(CoordSystem noc) { return (noc == CoordSystem::NOC0) ? 0 : 1; }

    uint32_t get_dram_noc_port(CoreCoord core) {
        if (core.coord_system == tt::CoordSystem::NOC0) {
            auto it = womrhole_dram_coord_to_noc_port_noc0.find({core.x, core.y});

            if (it != womrhole_dram_coord_to_noc_port_noc0.end()) {
                return it->second;
            }
        }

        if (core.coord_system == tt::CoordSystem::NOC1) {
            auto it = womrhole_dram_coord_to_noc_port_noc1.find({core.x, core.y});

            if (it != womrhole_dram_coord_to_noc_port_noc1.end()) {
                return it->second;
            }
        }

        return 0;
    }

    // clang-format off
    std::map<tt_xy_pair, uint32_t> womrhole_dram_coord_to_noc_port_noc0 {
        {{0, 1}, 0}, {{0, 11}, 1}, {{0, 0}, 2},   // Bank 0
        {{0, 7}, 0}, {{0, 5}, 1}, {{0, 6}, 2},    // Bank 1
        {{5, 1}, 0}, {{5, 11}, 1}, {{5, 0}, 2},   // Bank 2
        {{5, 10}, 0}, {{5, 2}, 1}, {{5, 9}, 2}, // Bank 3
        {{5, 4}, 0}, {{5, 8}, 1}, {{5, 3}, 2}, // Bank 4
        {{5, 7}, 0}, {{5, 5}, 1}, {{5, 6}, 2}  // Bank 5
    };

    std::map<tt_xy_pair, uint32_t> womrhole_dram_coord_to_noc_port_noc1 {
        {{9, 10}, 0}, {{9, 0}, 1}, {{9, 11}, 2},  // Bank 0     
        {{9, 4}, 0}, {{9, 6}, 1}, {{9, 5}, 2},    // Bank 1  
        {{4, 10}, 0}, {{4, 0}, 1}, {{4, 11}, 2},  // Bank 2    
        {{4, 1}, 0}, {{4, 9}, 1}, {{4, 2}, 2},  // Bank 3  
        {{4, 7}, 0}, {{4, 3}, 1}, {{4, 8}, 2}, // Bank 4  
        {{4, 4}, 0}, {{4, 6}, 1}, {{4, 5}, 2}  // Bank 5 
    };
    // clang-format on
};

TEST_F(TestNoc, TestNoc0NodeId) {
    auto arch = get_cluster()->get_cluster_description()->get_arch(0);
    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::TENSIX, CoordSystem::NOC0);
        check_noc_id_harvested_cores(chip, CoreType::TENSIX, CoordSystem::NOC0);

        check_noc_id_cores(chip, CoreType::ETH, CoordSystem::NOC0);
        check_noc_id_harvested_cores(chip, CoreType::ETH, CoordSystem::NOC0);

        if (arch == tt::ARCH::BLACKHOLE) {
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
    auto arch = get_cluster()->get_cluster_description()->get_arch(0);
    NocIdSwitcher noc1_switcher(NocId::NOC1);

    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::TENSIX, CoordSystem::NOC1);
        check_noc_id_harvested_cores(chip, CoreType::TENSIX, CoordSystem::NOC1);

        check_noc_id_cores(chip, CoreType::ETH, CoordSystem::NOC1);
        if (arch != tt::ARCH::BLACKHOLE) {
            check_noc_id_harvested_cores(chip, CoreType::ETH, CoordSystem::NOC1);
        }

        if (arch != tt::ARCH::WORMHOLE_B0) {
            check_noc_id_cores(chip, CoreType::DRAM, CoordSystem::NOC1);
            check_noc_id_harvested_cores(chip, CoreType::DRAM, CoordSystem::NOC1);
        }

        check_noc_id_cores(chip, CoreType::ARC, CoordSystem::NOC1);

        check_noc_id_cores(chip, CoreType::PCIE, CoordSystem::NOC1);

        // TODO: translated coordinate for harvested PCIE is not same on NOC0 and NOC1.
        // This needs to be fixed in some way in order for this to work on Blackhole
        // with enabled translation.
        if (arch != tt::ARCH::BLACKHOLE) {
            check_noc_id_harvested_cores(chip, CoreType::PCIE, CoordSystem::NOC1);
        }

        check_noc_id_cores(chip, CoreType::SECURITY, CoordSystem::NOC1);

        check_noc_id_cores(chip, CoreType::L2CPU, CoordSystem::NOC1);

        if (arch != tt::ARCH::BLACKHOLE) {
            check_noc_id_cores(chip, CoreType::ROUTER_ONLY, CoordSystem::NOC1);
        }
    }
}

class TestNocDramPorts : public TestNoc, public ::testing::WithParamInterface<NocId> {};

TEST_P(TestNocDramPorts, TestDramPortsWithNocSwitcher) {
    NocId noc_id = GetParam();
    NocIdSwitcher noc_switcher(noc_id);
    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        check_noc_id_cores(chip, CoreType::DRAM, CoordSystem::NOC0);
        check_noc_id_harvested_cores(chip, CoreType::DRAM, CoordSystem::NOC0);
        check_noc_id_cores(chip, CoreType::DRAM, CoordSystem::NOC1);
        check_noc_id_harvested_cores(chip, CoreType::DRAM, CoordSystem::NOC1);
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllNocIds,
    TestNocDramPorts,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return info.param == NocId::NOC0 ? "NOC0" : "NOC1"; });

class TestNocValidity : public TestNoc, public ::testing::WithParamInterface<std::tuple<CoreType, CoordSystem>> {};

TEST_P(TestNocValidity, VerifyNocTranslation) {
    auto [core_type, noc] = GetParam();
    auto arch = get_cluster()->get_cluster_description()->get_arch(0);

    // Skip ROUTER_ONLY on Blackhole - device-side mapping doesn't correlate with host-side.
    if (arch == ARCH::BLACKHOLE && core_type == CoreType::ROUTER_ONLY) {
        GTEST_SKIP() << "Mapping on device side does not correlate correctly to the mapping on host side";
    }

    // Determine which verification method to use based on architecture and core type.
    bool use_differ_test = false;
    if (arch == ARCH::BLACKHOLE) {
        use_differ_test =
            (core_type == CoreType::PCIE || core_type == CoreType::ARC || core_type == CoreType::SECURITY ||
             core_type == CoreType::L2CPU);
    } else if (arch == ARCH::WORMHOLE_B0) {
        use_differ_test =
            (core_type == CoreType::PCIE || core_type == CoreType::ARC || core_type == CoreType::ROUTER_ONLY);
    }

    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        if (use_differ_test) {
            verify_noc_ids_differ_by_noc(chip, core_type, noc);
        } else {
            verify_noc_id_cores_via_other_noc(chip, core_type, noc);
        }
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
