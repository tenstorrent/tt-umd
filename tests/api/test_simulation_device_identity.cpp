// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/simulation/simulation_device_identity.hpp"
#include "umd/device/soc_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

// describe_device(soc) -> build_soc_descriptor(info) reproduces a translation-equivalent
// descriptor: same arch, same grid size, same harvesting masks.
//
// Uses a Blackhole descriptor because DRAM/ETH harvesting are only supported on Blackhole
// (see CoordinateManager::assert_coordinate_manager_constructor); this lets the test exercise
// non-default values for all five harvesting mask fields, not just tensix.
TEST(SimulationDeviceIdentity, DescribeThenRebuildMatches) {
    ChipInfo chip_info{};
    chip_info.harvesting_masks.tensix_harvesting_mask = 0x1;
    chip_info.harvesting_masks.dram_harvesting_mask = 0x2;
    chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
    chip_info.harvesting_masks.l2cpu_harvesting_mask = 0x4;
    chip_info.harvesting_masks.pcie_harvesting_mask = 0x1;
    SocDescriptor original(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml")), chip_info);

    const SimulationServerDeviceInfo info = describe_device(original, SimulationBackendType::TTSim);
    EXPECT_EQ(info.status, 0);
    EXPECT_EQ(info.backend_type, SimulationBackendType::TTSim);
    EXPECT_FALSE(info.soc_descriptor_yaml.empty());
    EXPECT_EQ(info.tensix_harvesting_mask, 0x1u);

    const SocDescriptor rebuilt = build_soc_descriptor(info);
    EXPECT_EQ(rebuilt.arch, original.arch);
    EXPECT_EQ(rebuilt.get_arch_descriptor().get_grid_size(), original.get_arch_descriptor().get_grid_size());
    EXPECT_EQ(rebuilt.harvesting_masks.tensix_harvesting_mask, original.harvesting_masks.tensix_harvesting_mask);
    EXPECT_EQ(rebuilt.harvesting_masks.dram_harvesting_mask, original.harvesting_masks.dram_harvesting_mask);
    EXPECT_EQ(rebuilt.harvesting_masks.eth_harvesting_mask, original.harvesting_masks.eth_harvesting_mask);
    EXPECT_EQ(rebuilt.harvesting_masks.l2cpu_harvesting_mask, original.harvesting_masks.l2cpu_harvesting_mask);
    EXPECT_EQ(rebuilt.harvesting_masks.pcie_harvesting_mask, original.harvesting_masks.pcie_harvesting_mask);
}

// describe_cluster reads the build's cluster_descriptor.yaml text (dir/cluster_descriptor.yaml for a
// directory simulator path) so a client can reconstruct the topology.
TEST(SimulationDeviceIdentity, DescribeClusterReadsYamlWhenPresent) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("tt-umd-sim-clusterdesc-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string yaml = "chips:\n  0: [0, 0, 0, 0]\nethernet_connections: []\n";
    { std::ofstream(dir / "cluster_descriptor.yaml") << yaml; }

    const SimulationServerClusterDescriptor described = describe_cluster(dir);
    EXPECT_EQ(described.status, 0);
    EXPECT_EQ(described.yaml, yaml);

    fs::remove_all(dir);
}

// When the build ships no cluster_descriptor.yaml, describe_cluster returns an empty yaml (status 0),
// the signal for a client to fall back to a mock descriptor.
TEST(SimulationDeviceIdentity, DescribeClusterEmptyWhenNoYaml) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("tt-umd-sim-noclusterdesc-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    const SimulationServerClusterDescriptor described = describe_cluster(dir);
    EXPECT_EQ(described.status, 0);
    EXPECT_TRUE(described.yaml.empty());

    fs::remove_all(dir);
}
