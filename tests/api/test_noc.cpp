// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

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

    void verify_noc_id_cores_via_other_noc(
        ChipId chip, CoreType core_type, CoordSystem this_noc, bool use_harvested_cores) {
        CoordSystem other_noc = (this_noc == CoordSystem::NOC0) ? CoordSystem::NOC1 : CoordSystem::NOC0;

        // Set NOC context to this_noc for consistent read operations.
        NocIdSwitcher this_noc_switcher(static_cast<NocId>(get_noc_index(this_noc)));

        const std::vector<CoreCoord>& cores =
            use_harvested_cores ? cluster_->get_soc_descriptor(chip).get_harvested_cores(core_type, this_noc)
                                : cluster_->get_soc_descriptor(chip).get_cores(core_type, this_noc);

        for (const CoreCoord& core : cores) {
            // Read via this_noc the coordinate of the other_noc for the current core.
            const auto [other_x, other_y] = read_noc_id_reg(chip, core, get_noc_index(other_noc));

            // Read via this_noc the coordinate of the this_noc for the current core (for comparison).
            const auto [this_x, this_y] = read_noc_id_reg(chip, core, get_noc_index(this_noc));

            // Verify that reading this NOC's register returns coordinates matching the host-side core.
            EXPECT_EQ(core.x, this_x);
            EXPECT_EQ(core.y, this_y);

            // Represent the read coords in the system from which their regs were read.
            CoreCoord other_noc_coord(other_x, other_y, core_type, other_noc);

            // Translate the current from host-side core (which is represented in this_noc) to the other_noc.
            auto other_noc_coord_soc_desc = cluster_->get_soc_descriptor(chip).translate_coord_to(core, other_noc);

            log_debug(
                tt::LogUMD,
                "verify_noc_id_cores_via_other_noc: chip {} core ({},{}) this_noc={} read=({},{}) vs other_noc={} "
                "read=({},{}) expected=({},{})",
                chip,
                core.x,
                core.y,
                to_str(this_noc),
                this_x,
                this_y,
                to_str(other_noc),
                other_x,
                other_y,
                other_noc_coord_soc_desc.x,
                other_noc_coord_soc_desc.y);

            EXPECT_EQ(other_noc_coord.x, other_noc_coord_soc_desc.x);
            EXPECT_EQ(other_noc_coord.y, other_noc_coord_soc_desc.y);
        }
    }

    void verify_noc_ids_differ_by_noc(ChipId chip, CoreType core_type, CoordSystem this_noc, bool use_harvested_cores) {
        CoordSystem other_noc = (this_noc == CoordSystem::NOC0) ? CoordSystem::NOC1 : CoordSystem::NOC0;

        const std::vector<CoreCoord>& cores =
            use_harvested_cores ? cluster_->get_soc_descriptor(chip).get_harvested_cores(core_type, this_noc)
                                : cluster_->get_soc_descriptor(chip).get_cores(core_type, this_noc);

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

            log_debug(
                tt::LogUMD,
                "verify_noc_ids_differ_by_noc: chip {} core ({},{}) via_this_noc=({},{}) via_other_noc=({},{}) "
                "expected_other=({},{})",
                chip,
                core_this_noc.x,
                core_this_noc.y,
                other_noc_reg_value_via_this_noc.x,
                other_noc_reg_value_via_this_noc.y,
                other_noc_reg_value_via_other_noc.x,
                other_noc_reg_value_via_other_noc.y,
                core_other_noc.x,
                core_other_noc.y);

            // Reading via the other NOC returns coordinates matching that NOC's coordinate space.
            EXPECT_EQ(other_noc_reg_value_via_other_noc, core_other_noc);
        }
    }

    Cluster* get_cluster() { return cluster_.get(); };

    tt_xy_pair read_noc_translated_id_reg(ChipId chip, CoreCoord core, uint8_t noc_index) {
        auto noc_port = (core.core_type == CoreType::DRAM) ? get_dram_noc_port(core) : 0;
        const uint64_t noc_translated_id_reg_addr =
            cluster_->get_tt_device(0)->get_architecture_implementation()->get_noc_reg_base(
                core.core_type, noc_index, noc_port) +
            cluster_->get_tt_device(0)->get_architecture_implementation()->get_noc_id_logical_offset();

        uint32_t noc_translated_id_val;
        cluster_->read_from_device_reg(
            &noc_translated_id_val, chip, core, noc_translated_id_reg_addr, sizeof(noc_translated_id_val));

        uint32_t translated_x = noc_translated_id_val & 0x3F;
        uint32_t translated_y = (noc_translated_id_val >> 6) & 0x3F;

        return tt_xy_pair(translated_x, translated_y);
    }

private:
    std::unique_ptr<Cluster> cluster_;

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
            "Reading noc {} regs for chip {} core ({},{}) from addr {:x}. Result is raw {:x} which corresponds to "
            "core ({},{})",
            noc_index,
            chip,
            core.x,
            core.y,
            noc_node_id_reg_addr,
            noc_node_id_val,
            x,
            y);
        return tt_xy_pair(x, y);
    }

    static uint8_t get_noc_index(CoordSystem noc) { return (noc == CoordSystem::NOC0) ? 0 : 1; }

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

class TestNocValidity : public TestNoc,
                        public ::testing::WithParamInterface<std::tuple<CoreType, CoordSystem, bool>> {};

TEST_P(TestNocValidity, VerifyNocTranslationHostSide) {
    auto [core_type, noc, use_harvested_cores] = GetParam();
    auto arch = get_cluster()->get_cluster_description()->get_arch(0);

    // Skip ROUTER_ONLY on Blackhole - device-side mapping doesn't correlate with host-side.
    if (arch == ARCH::BLACKHOLE && core_type == CoreType::ROUTER_ONLY) {
        GTEST_SKIP() << "Mapping on device side does not correlate correctly to the mapping on host side";
    }

    // Skip ETH (NOC1) and PCIe (both NOCs) on Blackhole for harvested cores - well known problem:
    // - PCIe: https://github.com/tenstorrent/tt-umd/issues/826
    // - ETH: https://github.com/tenstorrent/tt-umd/issues/825
    if (arch == ARCH::BLACKHOLE && use_harvested_cores &&
        ((core_type == CoreType::ETH && noc == CoordSystem::NOC1) || core_type == CoreType::PCIE)) {
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
            verify_noc_ids_differ_by_noc(chip, core_type, noc, use_harvested_cores);
        } else {
            verify_noc_id_cores_via_other_noc(chip, core_type, noc, use_harvested_cores);
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
        ::testing::Values(CoordSystem::NOC0, CoordSystem::NOC1),
        ::testing::Values(false, true)),
    [](const ::testing::TestParamInfo<std::tuple<CoreType, CoordSystem, bool>>& info) {
        CoreType core_type = std::get<0>(info.param);
        CoordSystem noc = std::get<1>(info.param);
        bool use_harvested = std::get<2>(info.param);
        return to_str(core_type) + "_" + to_str(noc) + (use_harvested ? "_Harvested" : "_Normal");
    });

class TestNocTranslatedCoordinates : public TestNoc,
                                     public ::testing::WithParamInterface<std::tuple<CoreType, CoordSystem, uint8_t>> {
};

TEST_P(TestNocTranslatedCoordinates, VerifyNocIdTranslatedCoordinatesMatch) {
    auto [core_type, coord_system, noc_index] = GetParam();

    // Set NOC context for the transaction.
    NocIdSwitcher noc_switcher(static_cast<NocId>(noc_index));

    for (ChipId chip : get_cluster()->get_target_device_ids()) {
        // Get cores in the specified coordinate system.
        const std::vector<CoreCoord>& cores =
            get_cluster()->get_soc_descriptor(chip).get_cores(core_type, coord_system);

        for (const CoreCoord& core : cores) {
            // Read the translated coordinate register (should match TRANSLATED coordinate system).
            const auto [translated_x_reg, translated_y_reg] = read_noc_translated_id_reg(chip, core, noc_index);

            // Get the TRANSLATED coordinate from SocDescriptor.
            CoreCoord translated_coord =
                get_cluster()->get_soc_descriptor(chip).translate_coord_to(core, CoordSystem::TRANSLATED);

            log_debug(
                tt::LogUMD,
                "Chip {} {} core {}=({},{}) NOC{} -> TRANSLATED=({},{}) vs TRANSLATED_REG=({},{})",
                chip,
                to_str(core_type),
                to_str(coord_system),
                core.x,
                core.y,
                noc_index,
                translated_coord.x,
                translated_coord.y,
                translated_x_reg,
                translated_y_reg);

            // Verify that translated register coordinates match TRANSLATED coordinates.
            EXPECT_EQ(translated_x_reg, translated_coord.x)
                << "Chip " << chip << " " << to_str(core_type) << " core " << to_str(coord_system) << "=(" << core.x
                << "," << core.y << ") NOC" << static_cast<int>(noc_index) << " translated X mismatch";
            EXPECT_EQ(translated_y_reg, translated_coord.y)
                << "Chip " << chip << " " << to_str(core_type) << " core " << to_str(coord_system) << "=(" << core.x
                << "," << core.y << ") NOC" << static_cast<int>(noc_index) << " translated Y mismatch";
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllCoreTypesAndCoordSystems,
    TestNocTranslatedCoordinates,
    ::testing::Combine(
        ::testing::Values(CoreType::TENSIX, CoreType::DRAM, CoreType::ETH, CoreType::ARC, CoreType::PCIE),
        ::testing::Values(CoordSystem::NOC0, CoordSystem::NOC1),
        ::testing::Values(0, 1)),
    [](const ::testing::TestParamInfo<std::tuple<CoreType, CoordSystem, uint8_t>>& info) {
        CoreType core_type = std::get<0>(info.param);
        CoordSystem coord_system = std::get<1>(info.param);
        uint8_t noc_index = std::get<2>(info.param);
        return to_str(core_type) + "_on_" + to_str(coord_system) + "_via_NOC" + std::to_string(noc_index);
    });
