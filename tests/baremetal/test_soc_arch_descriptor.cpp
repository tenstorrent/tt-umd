// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

using namespace tt;
using namespace tt::umd;

// Test SocArchDescriptor creation from arch enum for Wormhole.
TEST(SocArchDescriptor, WormholeFromArch) {
    auto desc = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);

    EXPECT_EQ(desc.get_arch(), tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.get_grid_size(), wormhole::GRID_SIZE);
    EXPECT_EQ(desc.get_tensix_cores().size(), wormhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.get_tensix_cores(), wormhole::TENSIX_CORES_NOC0);
    EXPECT_EQ(desc.get_dram_cores().size(), wormhole::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.get_eth_cores().size(), wormhole::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.get_eth_cores(), wormhole::ETH_CORES_NOC0);
    EXPECT_EQ(desc.get_firmware_cores().size(), wormhole::ARC_CORES_NOC0.size());
    EXPECT_EQ(desc.get_firmware_cores(), wormhole::ARC_CORES_NOC0);
    EXPECT_EQ(desc.get_pcie_cores().size(), wormhole::PCIE_CORES_NOC0.size());
    EXPECT_EQ(desc.get_pcie_cores(), wormhole::PCIE_CORES_NOC0);
    EXPECT_EQ(desc.get_router_cores().size(), wormhole::ROUTER_CORES_NOC0.size());
    EXPECT_EQ(desc.get_worker_l1_size(), wormhole::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.get_eth_l1_size(), wormhole::ETH_L1_SIZE);
    EXPECT_EQ(desc.get_dram_bank_size(), wormhole::DRAM_BANK_SIZE);
    EXPECT_EQ(desc.get_noc0_x_to_noc1_x(), wormhole::NOC0_X_TO_NOC1_X);
    EXPECT_EQ(desc.get_noc0_y_to_noc1_y(), wormhole::NOC0_Y_TO_NOC1_Y);
    EXPECT_TRUE(desc.get_device_descriptor_file_path().empty());
}

// Test SocArchDescriptor creation from arch enum for Blackhole.
TEST(SocArchDescriptor, BlackholeFromArch) {
    auto desc = SocArchDescriptor(tt::ARCH::BLACKHOLE);

    EXPECT_EQ(desc.get_arch(), tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.get_grid_size(), blackhole::GRID_SIZE);
    EXPECT_EQ(desc.get_tensix_cores().size(), blackhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.get_tensix_cores(), blackhole::TENSIX_CORES_NOC0);
    EXPECT_EQ(desc.get_dram_cores().size(), blackhole::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.get_eth_cores().size(), blackhole::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.get_eth_cores(), blackhole::ETH_CORES_NOC0);
    EXPECT_EQ(desc.get_firmware_cores().size(), blackhole::ARC_CORES_NOC0.size());
    EXPECT_EQ(desc.get_pcie_cores().size(), blackhole::PCIE_CORES_NOC0.size());
    EXPECT_EQ(desc.get_worker_l1_size(), blackhole::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.get_eth_l1_size(), blackhole::ETH_L1_SIZE);
    EXPECT_EQ(desc.get_dram_bank_size(), blackhole::DRAM_BANK_SIZE);
    EXPECT_EQ(desc.get_noc0_x_to_noc1_x(), blackhole::NOC0_X_TO_NOC1_X);
    EXPECT_EQ(desc.get_noc0_y_to_noc1_y(), blackhole::NOC0_Y_TO_NOC1_Y);
}

// Test SocArchDescriptor creation from arch enum for Quasar.
TEST(SocArchDescriptor, QuasarFromArch) {
    auto desc = SocArchDescriptor(tt::ARCH::QUASAR);

    EXPECT_EQ(desc.get_arch(), tt::ARCH::QUASAR);
    EXPECT_EQ(desc.get_grid_size(), grendel::GRID_SIZE);
    EXPECT_EQ(desc.get_tensix_cores().size(), grendel::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.get_dram_cores().size(), grendel::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.get_eth_cores().size(), grendel::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.get_worker_l1_size(), grendel::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.get_eth_l1_size(), grendel::ETH_L1_SIZE);
    EXPECT_EQ(desc.get_dram_bank_size(), grendel::DRAM_BANK_SIZE);
}

// Test SocArchDescriptor creation from YAML for Wormhole.
TEST(SocArchDescriptor, WormholeFromYaml) {
    auto desc = SocArchDescriptor(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"));

    EXPECT_EQ(desc.get_arch(), tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.get_grid_size(), wormhole::GRID_SIZE);
    EXPECT_EQ(desc.get_tensix_cores().size(), wormhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.get_dram_cores().size(), wormhole::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.get_eth_cores().size(), wormhole::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.get_firmware_cores().size(), wormhole::ARC_CORES_NOC0.size());
    EXPECT_EQ(desc.get_pcie_cores().size(), wormhole::PCIE_CORES_NOC0.size());
    EXPECT_FALSE(desc.get_device_descriptor_file_path().empty());
}

// Test SocArchDescriptor creation from YAML for Blackhole.
TEST(SocArchDescriptor, BlackholeFromYaml) {
    auto desc = SocArchDescriptor(test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"));

    EXPECT_EQ(desc.get_arch(), tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.get_grid_size(), blackhole::GRID_SIZE);
    EXPECT_EQ(desc.get_tensix_cores().size(), blackhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.get_dram_cores().size(), blackhole::DRAM_CORES_NOC0.size());
}

// Test that arch enum and YAML produce consistent results for Wormhole.
TEST(SocArchDescriptor, WormholeArchAndYamlConsistency) {
    auto from_arch = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);
    auto from_yaml = SocArchDescriptor(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"));

    EXPECT_EQ(from_arch.get_arch(), from_yaml.get_arch());
    EXPECT_EQ(from_arch.get_grid_size(), from_yaml.get_grid_size());
    EXPECT_EQ(from_arch.get_tensix_cores(), from_yaml.get_tensix_cores());
    EXPECT_EQ(from_arch.get_eth_cores(), from_yaml.get_eth_cores());
    EXPECT_EQ(from_arch.get_firmware_cores(), from_yaml.get_firmware_cores());
    EXPECT_EQ(from_arch.get_pcie_cores(), from_yaml.get_pcie_cores());
    EXPECT_EQ(from_arch.get_router_cores(), from_yaml.get_router_cores());
    EXPECT_EQ(from_arch.get_worker_l1_size(), from_yaml.get_worker_l1_size());
    EXPECT_EQ(from_arch.get_eth_l1_size(), from_yaml.get_eth_l1_size());
    EXPECT_EQ(from_arch.get_dram_bank_size(), from_yaml.get_dram_bank_size());

    // DRAM cores: compare per-channel.
    ASSERT_EQ(from_arch.get_dram_cores().size(), from_yaml.get_dram_cores().size());
    for (size_t i = 0; i < from_arch.get_dram_cores().size(); i++) {
        EXPECT_EQ(from_arch.get_dram_cores()[i], from_yaml.get_dram_cores()[i]);
    }
}

// Test that arch enum and YAML produce consistent results for Blackhole.
TEST(SocArchDescriptor, BlackholeArchAndYamlConsistency) {
    auto from_arch = SocArchDescriptor(tt::ARCH::BLACKHOLE);
    auto from_yaml = SocArchDescriptor(test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"));

    EXPECT_EQ(from_arch.get_arch(), from_yaml.get_arch());
    EXPECT_EQ(from_arch.get_grid_size(), from_yaml.get_grid_size());
    EXPECT_EQ(from_arch.get_tensix_cores(), from_yaml.get_tensix_cores());
    EXPECT_EQ(from_arch.get_firmware_cores(), from_yaml.get_firmware_cores());
    EXPECT_EQ(from_arch.get_pcie_cores(), from_yaml.get_pcie_cores());
    EXPECT_EQ(from_arch.get_worker_l1_size(), from_yaml.get_worker_l1_size());
    EXPECT_EQ(from_arch.get_eth_l1_size(), from_yaml.get_eth_l1_size());
    EXPECT_EQ(from_arch.get_dram_bank_size(), from_yaml.get_dram_bank_size());

    ASSERT_EQ(from_arch.get_dram_cores().size(), from_yaml.get_dram_cores().size());
    for (size_t i = 0; i < from_arch.get_dram_cores().size(); i++) {
        EXPECT_EQ(from_arch.get_dram_cores()[i], from_yaml.get_dram_cores()[i]);
    }
}

// Test derived data: cores map is populated correctly.
TEST(SocArchDescriptor, WormholeDerivedCoresMap) {
    auto desc = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);

    // Total cores in the map should equal sum of all core type vectors.
    size_t expected_total = desc.get_tensix_cores().size() + desc.get_eth_cores().size() +
                            desc.get_firmware_cores().size() + desc.get_pcie_cores().size() +
                            desc.get_router_cores().size() + desc.get_security_cores().size() +
                            desc.get_l2cpu_cores().size();
    for (const auto& channel : desc.get_dram_cores()) {
        expected_total += channel.size();
    }
    EXPECT_EQ(desc.get_cores().size(), expected_total);

    // Verify tensix cores are in the map with correct type.
    for (const auto& core : desc.get_tensix_cores()) {
        auto it = desc.get_cores().find(core);
        ASSERT_NE(it, desc.get_cores().end());
        EXPECT_EQ(it->second.type, CoreType::WORKER);
        EXPECT_EQ(it->second.l1_size, desc.get_worker_l1_size());
    }

    // Verify ETH cores are in the map with correct type.
    for (const auto& core : desc.get_eth_cores()) {
        auto it = desc.get_cores().find(core);
        ASSERT_NE(it, desc.get_cores().end());
        EXPECT_EQ(it->second.type, CoreType::ETH);
        EXPECT_EQ(it->second.l1_size, desc.get_eth_l1_size());
    }

    // Verify ARC cores.
    for (const auto& core : desc.get_firmware_cores()) {
        auto it = desc.get_cores().find(core);
        ASSERT_NE(it, desc.get_cores().end());
        EXPECT_EQ(it->second.type, CoreType::ARC);
    }

    // Verify PCIE cores.
    for (const auto& core : desc.get_pcie_cores()) {
        auto it = desc.get_cores().find(core);
        ASSERT_NE(it, desc.get_cores().end());
        EXPECT_EQ(it->second.type, CoreType::PCIE);
    }
}

// Test derived data: DRAM channel map.
TEST(SocArchDescriptor, WormholeDRAMChannelMap) {
    auto desc = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);

    EXPECT_EQ(desc.get_dram_cores().size(), wormhole::NUM_DRAM_BANKS);
    for (size_t channel = 0; channel < desc.get_dram_cores().size(); channel++) {
        for (size_t subchannel = 0; subchannel < desc.get_dram_cores()[channel].size(); subchannel++) {
            const tt_xy_pair& core = desc.get_dram_cores()[channel][subchannel];
            auto it = desc.get_dram_core_channel_map().find(core);
            ASSERT_NE(it, desc.get_dram_core_channel_map().end());
            EXPECT_EQ(std::get<0>(it->second), static_cast<int>(channel));
            EXPECT_EQ(std::get<1>(it->second), static_cast<int>(subchannel));
        }
    }
}

// Test derived data: ETH channel map.
TEST(SocArchDescriptor, WormholeETHChannelMap) {
    auto desc = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);

    EXPECT_EQ(desc.get_eth_cores().size(), wormhole::ETH_CORES_NOC0.size());
    for (size_t channel = 0; channel < desc.get_eth_cores().size(); channel++) {
        auto it = desc.get_ethernet_core_channel_map().find(desc.get_eth_cores()[channel]);
        ASSERT_NE(it, desc.get_ethernet_core_channel_map().end());
        EXPECT_EQ(it->second, static_cast<int>(channel));
    }
}

// Test derived data: worker grid size.
TEST(SocArchDescriptor, WormholeWorkerGridSize) {
    auto desc = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.get_worker_grid_size(), wormhole::TENSIX_GRID_SIZE);
}

TEST(SocArchDescriptor, BlackholeWorkerGridSize) {
    auto desc = SocArchDescriptor(tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.get_worker_grid_size(), blackhole::TENSIX_GRID_SIZE);
}

// Test static helpers.
TEST(SocArchDescriptor, GetArchFromPath) {
    EXPECT_EQ(
        SocArchDescriptor::get_arch_from_path(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml")),
        tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(
        SocArchDescriptor::get_arch_from_path(test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml")),
        tt::ARCH::BLACKHOLE);
}

TEST(SocArchDescriptor, GetGridSizeFromPath) {
    EXPECT_EQ(
        SocArchDescriptor::get_grid_size_from_path(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml")),
        wormhole::GRID_SIZE);
    EXPECT_EQ(
        SocArchDescriptor::get_grid_size_from_path(test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml")),
        blackhole::GRID_SIZE);
}

// Test invalid arch throws.
TEST(SocArchDescriptor, InvalidArchThrows) {
    EXPECT_THROW(SocArchDescriptor{tt::ARCH::Invalid}, error::UmdException<error::RuntimeError>);
}

// Test invalid YAML path throws.
TEST(SocArchDescriptor, InvalidPathThrows) {
    EXPECT_THROW(SocArchDescriptor("/nonexistent/path.yaml"), error::UmdException<error::RuntimeError>);
}

// Test calculate_grid_size utility.
TEST(SocArchDescriptor, CalculateGridSize) {
    std::vector<tt_xy_pair> cores = {{1, 2}, {1, 3}, {2, 2}, {2, 3}, {3, 2}, {3, 3}};
    tt_xy_pair grid = SocArchDescriptor::calculate_grid_size(cores);
    EXPECT_EQ(grid.x, 3);
    EXPECT_EQ(grid.y, 2);

    std::vector<tt_xy_pair> empty_cores = {};
    tt_xy_pair empty_grid = SocArchDescriptor::calculate_grid_size(empty_cores);
    EXPECT_EQ(empty_grid.x, 0);
    EXPECT_EQ(empty_grid.y, 0);
}

// Test Blackhole derived data.
TEST(SocArchDescriptor, BlackholeDRAMChannelMap) {
    auto desc = SocArchDescriptor(tt::ARCH::BLACKHOLE);

    EXPECT_EQ(desc.get_dram_cores().size(), blackhole::NUM_DRAM_BANKS);
    for (size_t channel = 0; channel < desc.get_dram_cores().size(); channel++) {
        EXPECT_EQ(desc.get_dram_cores()[channel].size(), blackhole::NUM_NOC_PORTS_PER_DRAM_BANK);
        for (size_t subchannel = 0; subchannel < desc.get_dram_cores()[channel].size(); subchannel++) {
            const tt_xy_pair& core = desc.get_dram_cores()[channel][subchannel];
            auto it = desc.get_dram_core_channel_map().find(core);
            ASSERT_NE(it, desc.get_dram_core_channel_map().end());
            EXPECT_EQ(std::get<0>(it->second), static_cast<int>(channel));
            EXPECT_EQ(std::get<1>(it->second), static_cast<int>(subchannel));
        }
    }
}

// Test Wormhole: no security and no L2CPU cores.
TEST(SocArchDescriptor, WormholeNoSecurityNoL2CPU) {
    auto desc = SocArchDescriptor(tt::ARCH::WORMHOLE_B0);
    EXPECT_TRUE(desc.get_security_cores().empty());
    EXPECT_TRUE(desc.get_l2cpu_cores().empty());
}

// Test Blackhole: has security and L2CPU cores.
TEST(SocArchDescriptor, BlackholeHasSecurityAndL2CPU) {
    auto desc = SocArchDescriptor(tt::ARCH::BLACKHOLE);
    EXPECT_FALSE(desc.get_security_cores().empty());
    EXPECT_FALSE(desc.get_l2cpu_cores().empty());
}

// Test Blackhole simulation 1x2 YAML descriptor.
TEST(SocArchDescriptor, BlackholeSimulation1x2) {
    auto desc = SocArchDescriptor(test_utils::GetSocDescAbsPath("blackhole_simulation_1x2.yaml"));

    EXPECT_EQ(desc.get_arch(), tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.get_grid_size(), tt_xy_pair(2, 2));

    // 2 tensix cores: (0,1) and (1,1).
    EXPECT_EQ(desc.get_tensix_cores().size(), 2);
    EXPECT_EQ(desc.get_tensix_cores()[0], tt_xy_pair(0, 1));
    EXPECT_EQ(desc.get_tensix_cores()[1], tt_xy_pair(1, 1));

    // 1 DRAM bank with 1 core: (1,0).
    ASSERT_EQ(desc.get_dram_cores().size(), 1);
    ASSERT_EQ(desc.get_dram_cores()[0].size(), 1);
    EXPECT_EQ(desc.get_dram_cores()[0][0], tt_xy_pair(1, 0));

    // 1 router core: (0,0).
    EXPECT_EQ(desc.get_router_cores().size(), 1);
    EXPECT_EQ(desc.get_router_cores()[0], tt_xy_pair(0, 0));

    // No ARC, PCIe, ETH cores.
    EXPECT_TRUE(desc.get_firmware_cores().empty());
    EXPECT_TRUE(desc.get_pcie_cores().empty());
    EXPECT_TRUE(desc.get_eth_cores().empty());

    // Memory sizes.
    EXPECT_EQ(desc.get_worker_l1_size(), 1572864);
    EXPECT_EQ(desc.get_dram_bank_size(), 1073741824);
    EXPECT_EQ(desc.get_eth_l1_size(), 0);

    // Worker grid size: 2x1.
    EXPECT_EQ(desc.get_worker_grid_size(), tt_xy_pair(2, 1));

    // NOC mappings.
    ASSERT_EQ(desc.get_noc0_x_to_noc1_x().size(), 2);
    EXPECT_EQ(desc.get_noc0_x_to_noc1_x()[0], 1);
    EXPECT_EQ(desc.get_noc0_x_to_noc1_x()[1], 0);
    ASSERT_EQ(desc.get_noc0_y_to_noc1_y().size(), 2);
    EXPECT_EQ(desc.get_noc0_y_to_noc1_y()[0], 1);
    EXPECT_EQ(desc.get_noc0_y_to_noc1_y()[1], 0);
}

// Test Quasar simulation 1x1 YAML descriptor.
TEST(SocArchDescriptor, QuasarSimulation1x1) {
    auto desc = SocArchDescriptor(test_utils::GetSocDescAbsPath("quasar_simulation_1x1.yaml"));

    EXPECT_EQ(desc.get_arch(), tt::ARCH::QUASAR);
    EXPECT_EQ(desc.get_grid_size(), tt_xy_pair(1, 3));

    // 1 tensix core: (0,1).
    ASSERT_EQ(desc.get_tensix_cores().size(), 1);
    EXPECT_EQ(desc.get_tensix_cores()[0], tt_xy_pair(0, 1));

    // 1 DRAM bank with 1 core: (0,0).
    ASSERT_EQ(desc.get_dram_cores().size(), 1);
    ASSERT_EQ(desc.get_dram_cores()[0].size(), 1);
    EXPECT_EQ(desc.get_dram_cores()[0][0], tt_xy_pair(0, 0));

    // 1 router core: (0,2).
    EXPECT_EQ(desc.get_router_cores().size(), 1);
    EXPECT_EQ(desc.get_router_cores()[0], tt_xy_pair(0, 2));

    // No ARC, PCIe, ETH cores.
    EXPECT_TRUE(desc.get_firmware_cores().empty());
    EXPECT_TRUE(desc.get_pcie_cores().empty());
    EXPECT_TRUE(desc.get_eth_cores().empty());

    // Memory sizes.
    EXPECT_EQ(desc.get_worker_l1_size(), 4194304);
    EXPECT_EQ(desc.get_dram_bank_size(), 1073741824);
    EXPECT_EQ(desc.get_eth_l1_size(), 0);

    // Worker grid size: 1x1.
    EXPECT_EQ(desc.get_worker_grid_size(), tt_xy_pair(1, 1));
}

// Test Wormhole B0 1x1 YAML descriptor.
TEST(SocArchDescriptor, WormholeB01x1) {
    auto desc = SocArchDescriptor(test_utils::GetSocDescAbsPath("wormhole_b0_1x1.yaml"));

    EXPECT_EQ(desc.get_arch(), tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.get_grid_size(), tt_xy_pair(10, 12));

    // 1 tensix core: (1,1).
    ASSERT_EQ(desc.get_tensix_cores().size(), 1);
    EXPECT_EQ(desc.get_tensix_cores()[0], tt_xy_pair(1, 1));

    // 6 DRAM banks with 3 ports each.
    ASSERT_EQ(desc.get_dram_cores().size(), 6);
    for (const auto& bank : desc.get_dram_cores()) {
        EXPECT_EQ(bank.size(), 3);
    }

    // 16 ETH cores.
    EXPECT_EQ(desc.get_eth_cores().size(), 16);

    // 1 ARC core: (0,10).
    ASSERT_EQ(desc.get_firmware_cores().size(), 1);
    EXPECT_EQ(desc.get_firmware_cores()[0], tt_xy_pair(0, 10));

    // 1 PCIe core: (0,3).
    ASSERT_EQ(desc.get_pcie_cores().size(), 1);
    EXPECT_EQ(desc.get_pcie_cores()[0], tt_xy_pair(0, 3));

    // Memory sizes match wormhole constants.
    EXPECT_EQ(desc.get_worker_l1_size(), wormhole::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.get_dram_bank_size(), wormhole::DRAM_BANK_SIZE);
    EXPECT_EQ(desc.get_eth_l1_size(), wormhole::ETH_L1_SIZE);

    // Worker grid size: 1x1.
    EXPECT_EQ(desc.get_worker_grid_size(), tt_xy_pair(1, 1));

    // NOC mappings should be the same as full wormhole.
    EXPECT_EQ(desc.get_noc0_x_to_noc1_x(), wormhole::NOC0_X_TO_NOC1_X);
    EXPECT_EQ(desc.get_noc0_y_to_noc1_y(), wormhole::NOC0_Y_TO_NOC1_Y);

    // Many router cores (grid is full 10x12 but only 1 tensix worker).
    EXPECT_FALSE(desc.get_router_cores().empty());
}

// Test all available YAML descriptors can be loaded.
TEST(SocArchDescriptor, AllSocDescriptors) {
    for (const std::string& soc_desc_yaml : test_utils::GetAllSocDescs()) {
        EXPECT_NO_THROW(SocArchDescriptor{soc_desc_yaml}) << "Failed to load: " << soc_desc_yaml;
    }
}
