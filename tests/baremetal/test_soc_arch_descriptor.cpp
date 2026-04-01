// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/soc_arch_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

// Test SocArchDescriptor creation from arch enum for Wormhole.
TEST(SocArchDescriptor, WormholeFromArch) {
    auto desc = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);

    EXPECT_EQ(desc.arch, tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.grid_size, wormhole::GRID_SIZE);
    EXPECT_EQ(desc.tensix_cores.size(), wormhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.tensix_cores, wormhole::TENSIX_CORES_NOC0);
    EXPECT_EQ(desc.dram_cores.size(), wormhole::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.eth_cores.size(), wormhole::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.eth_cores, wormhole::ETH_CORES_NOC0);
    EXPECT_EQ(desc.arc_cores.size(), wormhole::ARC_CORES_NOC0.size());
    EXPECT_EQ(desc.arc_cores, wormhole::ARC_CORES_NOC0);
    EXPECT_EQ(desc.pcie_cores.size(), wormhole::PCIE_CORES_NOC0.size());
    EXPECT_EQ(desc.pcie_cores, wormhole::PCIE_CORES_NOC0);
    EXPECT_EQ(desc.router_cores.size(), wormhole::ROUTER_CORES_NOC0.size());
    EXPECT_EQ(desc.worker_l1_size, wormhole::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.eth_l1_size, wormhole::ETH_L1_SIZE);
    EXPECT_EQ(desc.dram_bank_size, wormhole::DRAM_BANK_SIZE);
    EXPECT_EQ(desc.noc0_x_to_noc1_x, wormhole::NOC0_X_TO_NOC1_X);
    EXPECT_EQ(desc.noc0_y_to_noc1_y, wormhole::NOC0_Y_TO_NOC1_Y);
    EXPECT_TRUE(desc.device_descriptor_file_path.empty());
}

// Test SocArchDescriptor creation from arch enum for Blackhole.
TEST(SocArchDescriptor, BlackholeFromArch) {
    auto desc = SocArchDescriptor::create(tt::ARCH::BLACKHOLE);

    EXPECT_EQ(desc.arch, tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.grid_size, blackhole::GRID_SIZE);
    EXPECT_EQ(desc.tensix_cores.size(), blackhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.tensix_cores, blackhole::TENSIX_CORES_NOC0);
    EXPECT_EQ(desc.dram_cores.size(), blackhole::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.eth_cores.size(), blackhole::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.eth_cores, blackhole::ETH_CORES_NOC0);
    EXPECT_EQ(desc.arc_cores.size(), blackhole::ARC_CORES_NOC0.size());
    EXPECT_EQ(desc.pcie_cores.size(), blackhole::PCIE_CORES_NOC0.size());
    EXPECT_EQ(desc.worker_l1_size, blackhole::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.eth_l1_size, blackhole::ETH_L1_SIZE);
    EXPECT_EQ(desc.dram_bank_size, blackhole::DRAM_BANK_SIZE);
    EXPECT_EQ(desc.noc0_x_to_noc1_x, blackhole::NOC0_X_TO_NOC1_X);
    EXPECT_EQ(desc.noc0_y_to_noc1_y, blackhole::NOC0_Y_TO_NOC1_Y);
}

// Test SocArchDescriptor creation from arch enum for Quasar.
TEST(SocArchDescriptor, QuasarFromArch) {
    auto desc = SocArchDescriptor::create(tt::ARCH::QUASAR);

    EXPECT_EQ(desc.arch, tt::ARCH::QUASAR);
    EXPECT_EQ(desc.grid_size, grendel::GRID_SIZE);
    EXPECT_EQ(desc.tensix_cores.size(), grendel::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.dram_cores.size(), grendel::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.eth_cores.size(), grendel::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.worker_l1_size, grendel::TENSIX_L1_SIZE);
    EXPECT_EQ(desc.eth_l1_size, grendel::ETH_L1_SIZE);
    EXPECT_EQ(desc.dram_bank_size, grendel::DRAM_BANK_SIZE);
}

// Test SocArchDescriptor creation from YAML for Wormhole.
TEST(SocArchDescriptor, WormholeFromYaml) {
    auto desc = SocArchDescriptor::create(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"));

    EXPECT_EQ(desc.arch, tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.grid_size, wormhole::GRID_SIZE);
    EXPECT_EQ(desc.tensix_cores.size(), wormhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.dram_cores.size(), wormhole::DRAM_CORES_NOC0.size());
    EXPECT_EQ(desc.eth_cores.size(), wormhole::ETH_CORES_NOC0.size());
    EXPECT_EQ(desc.arc_cores.size(), wormhole::ARC_CORES_NOC0.size());
    EXPECT_EQ(desc.pcie_cores.size(), wormhole::PCIE_CORES_NOC0.size());
    EXPECT_FALSE(desc.device_descriptor_file_path.empty());
}

// Test SocArchDescriptor creation from YAML for Blackhole.
TEST(SocArchDescriptor, BlackholeFromYaml) {
    auto desc = SocArchDescriptor::create(test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"));

    EXPECT_EQ(desc.arch, tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.grid_size, blackhole::GRID_SIZE);
    EXPECT_EQ(desc.tensix_cores.size(), blackhole::TENSIX_CORES_NOC0.size());
    EXPECT_EQ(desc.dram_cores.size(), blackhole::DRAM_CORES_NOC0.size());
}

// Test that arch enum and YAML produce consistent results for Wormhole.
TEST(SocArchDescriptor, WormholeArchAndYamlConsistency) {
    auto from_arch = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);
    auto from_yaml = SocArchDescriptor::create(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"));

    EXPECT_EQ(from_arch.arch, from_yaml.arch);
    EXPECT_EQ(from_arch.grid_size, from_yaml.grid_size);
    EXPECT_EQ(from_arch.tensix_cores, from_yaml.tensix_cores);
    EXPECT_EQ(from_arch.eth_cores, from_yaml.eth_cores);
    EXPECT_EQ(from_arch.arc_cores, from_yaml.arc_cores);
    EXPECT_EQ(from_arch.pcie_cores, from_yaml.pcie_cores);
    EXPECT_EQ(from_arch.router_cores, from_yaml.router_cores);
    EXPECT_EQ(from_arch.worker_l1_size, from_yaml.worker_l1_size);
    EXPECT_EQ(from_arch.eth_l1_size, from_yaml.eth_l1_size);
    EXPECT_EQ(from_arch.dram_bank_size, from_yaml.dram_bank_size);

    // DRAM cores: compare per-channel.
    ASSERT_EQ(from_arch.dram_cores.size(), from_yaml.dram_cores.size());
    for (size_t i = 0; i < from_arch.dram_cores.size(); i++) {
        EXPECT_EQ(from_arch.dram_cores[i], from_yaml.dram_cores[i]);
    }
}

// Test that arch enum and YAML produce consistent results for Blackhole.
TEST(SocArchDescriptor, BlackholeArchAndYamlConsistency) {
    auto from_arch = SocArchDescriptor::create(tt::ARCH::BLACKHOLE);
    auto from_yaml = SocArchDescriptor::create(test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"));

    EXPECT_EQ(from_arch.arch, from_yaml.arch);
    EXPECT_EQ(from_arch.grid_size, from_yaml.grid_size);
    EXPECT_EQ(from_arch.tensix_cores, from_yaml.tensix_cores);
    EXPECT_EQ(from_arch.arc_cores, from_yaml.arc_cores);
    EXPECT_EQ(from_arch.pcie_cores, from_yaml.pcie_cores);
    EXPECT_EQ(from_arch.worker_l1_size, from_yaml.worker_l1_size);
    EXPECT_EQ(from_arch.eth_l1_size, from_yaml.eth_l1_size);
    EXPECT_EQ(from_arch.dram_bank_size, from_yaml.dram_bank_size);

    ASSERT_EQ(from_arch.dram_cores.size(), from_yaml.dram_cores.size());
    for (size_t i = 0; i < from_arch.dram_cores.size(); i++) {
        EXPECT_EQ(from_arch.dram_cores[i], from_yaml.dram_cores[i]);
    }
}

// Test derived data: cores map is populated correctly.
TEST(SocArchDescriptor, WormholeDerivedCoresMap) {
    auto desc = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);

    // Total cores in the map should equal sum of all core type vectors.
    size_t expected_total = desc.tensix_cores.size() + desc.eth_cores.size() + desc.arc_cores.size() +
                            desc.pcie_cores.size() + desc.router_cores.size() + desc.security_cores.size() +
                            desc.l2cpu_cores.size();
    for (const auto& channel : desc.dram_cores) {
        expected_total += channel.size();
    }
    EXPECT_EQ(desc.cores.size(), expected_total);

    // Verify tensix cores are in the map with correct type.
    for (const auto& core : desc.tensix_cores) {
        auto it = desc.cores.find(core);
        ASSERT_NE(it, desc.cores.end());
        EXPECT_EQ(it->second.type, CoreType::WORKER);
        EXPECT_EQ(it->second.l1_size, desc.worker_l1_size);
    }

    // Verify ETH cores are in the map with correct type.
    for (const auto& core : desc.eth_cores) {
        auto it = desc.cores.find(core);
        ASSERT_NE(it, desc.cores.end());
        EXPECT_EQ(it->second.type, CoreType::ETH);
        EXPECT_EQ(it->second.l1_size, desc.eth_l1_size);
    }

    // Verify ARC cores.
    for (const auto& core : desc.arc_cores) {
        auto it = desc.cores.find(core);
        ASSERT_NE(it, desc.cores.end());
        EXPECT_EQ(it->second.type, CoreType::ARC);
    }

    // Verify PCIE cores.
    for (const auto& core : desc.pcie_cores) {
        auto it = desc.cores.find(core);
        ASSERT_NE(it, desc.cores.end());
        EXPECT_EQ(it->second.type, CoreType::PCIE);
    }
}

// Test derived data: DRAM channel map.
TEST(SocArchDescriptor, WormholeDRAMChannelMap) {
    auto desc = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);

    EXPECT_EQ(desc.dram_cores.size(), wormhole::NUM_DRAM_BANKS);
    for (size_t channel = 0; channel < desc.dram_cores.size(); channel++) {
        for (size_t subchannel = 0; subchannel < desc.dram_cores[channel].size(); subchannel++) {
            const tt_xy_pair& core = desc.dram_cores[channel][subchannel];
            auto it = desc.dram_core_channel_map.find(core);
            ASSERT_NE(it, desc.dram_core_channel_map.end());
            EXPECT_EQ(std::get<0>(it->second), static_cast<int>(channel));
            EXPECT_EQ(std::get<1>(it->second), static_cast<int>(subchannel));
        }
    }
}

// Test derived data: ETH channel map.
TEST(SocArchDescriptor, WormholeETHChannelMap) {
    auto desc = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);

    EXPECT_EQ(desc.eth_cores.size(), wormhole::ETH_CORES_NOC0.size());
    for (size_t channel = 0; channel < desc.eth_cores.size(); channel++) {
        auto it = desc.ethernet_core_channel_map.find(desc.eth_cores[channel]);
        ASSERT_NE(it, desc.ethernet_core_channel_map.end());
        EXPECT_EQ(it->second, static_cast<int>(channel));
    }
}

// Test derived data: worker grid size.
TEST(SocArchDescriptor, WormholeWorkerGridSize) {
    auto desc = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(desc.worker_grid_size, wormhole::TENSIX_GRID_SIZE);
}

TEST(SocArchDescriptor, BlackholeWorkerGridSize) {
    auto desc = SocArchDescriptor::create(tt::ARCH::BLACKHOLE);
    EXPECT_EQ(desc.worker_grid_size, blackhole::TENSIX_GRID_SIZE);
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
    EXPECT_THROW(SocArchDescriptor::create(tt::ARCH::Invalid), std::runtime_error);
}

// Test invalid YAML path throws.
TEST(SocArchDescriptor, InvalidPathThrows) {
    EXPECT_THROW(SocArchDescriptor::create("/nonexistent/path.yaml"), std::runtime_error);
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
    auto desc = SocArchDescriptor::create(tt::ARCH::BLACKHOLE);

    EXPECT_EQ(desc.dram_cores.size(), blackhole::NUM_DRAM_BANKS);
    for (size_t channel = 0; channel < desc.dram_cores.size(); channel++) {
        EXPECT_EQ(desc.dram_cores[channel].size(), blackhole::NUM_NOC_PORTS_PER_DRAM_BANK);
        for (size_t subchannel = 0; subchannel < desc.dram_cores[channel].size(); subchannel++) {
            const tt_xy_pair& core = desc.dram_cores[channel][subchannel];
            auto it = desc.dram_core_channel_map.find(core);
            ASSERT_NE(it, desc.dram_core_channel_map.end());
            EXPECT_EQ(std::get<0>(it->second), static_cast<int>(channel));
            EXPECT_EQ(std::get<1>(it->second), static_cast<int>(subchannel));
        }
    }
}

// Test Wormhole: no security and no L2CPU cores.
TEST(SocArchDescriptor, WormholeNoSecurityNoL2CPU) {
    auto desc = SocArchDescriptor::create(tt::ARCH::WORMHOLE_B0);
    EXPECT_TRUE(desc.security_cores.empty());
    EXPECT_TRUE(desc.l2cpu_cores.empty());
}

// Test Blackhole: has security and L2CPU cores.
TEST(SocArchDescriptor, BlackholeHasSecurityAndL2CPU) {
    auto desc = SocArchDescriptor::create(tt::ARCH::BLACKHOLE);
    EXPECT_FALSE(desc.security_cores.empty());
    EXPECT_FALSE(desc.l2cpu_cores.empty());
}

// Test all available YAML descriptors can be loaded.
TEST(SocArchDescriptor, AllSocDescriptors) {
    for (const std::string& soc_desc_yaml : test_utils::GetAllSocDescs()) {
        EXPECT_NO_THROW(SocArchDescriptor::create(soc_desc_yaml)) << "Failed to load: " << soc_desc_yaml;
    }
}
