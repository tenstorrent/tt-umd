// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The public simulator queries that hold in every build of UMD: where a simulator's SoC descriptor is, and that a
// device reached through the KMD is backed by no simulator.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/tt_device/tt_device.hpp"

using namespace tt::umd;
using tt::umd::test_utils::SimulatorLayout;

TEST(SimulatorInfo, FindsSocDescriptorBesideTTSimLibrary) {
    const SimulatorLayout simulator;
    const std::filesystem::path library = simulator.add_ttsim_library();
    const std::filesystem::path soc_descriptor = simulator.add_soc_descriptor("quasar_32_arch.yaml");

    const std::string found = find_simulator_soc_descriptor_path(library);

    ASSERT_FALSE(found.empty());
    EXPECT_TRUE(std::filesystem::equivalent(found, soc_descriptor));
}

TEST(SimulatorInfo, TTSimLibraryWithoutSocDescriptorShipsNone) {
    const SimulatorLayout simulator;

    EXPECT_EQ(find_simulator_soc_descriptor_path(simulator.add_ttsim_library()), "");
}

TEST(SimulatorInfo, FindsSocDescriptorInsideRtlDirectory) {
    const SimulatorLayout simulator;
    const std::filesystem::path soc_descriptor = simulator.add_soc_descriptor("quasar_32_arch.yaml");

    const std::string found = find_simulator_soc_descriptor_path(simulator.directory());

    ASSERT_FALSE(found.empty());
    EXPECT_TRUE(std::filesystem::equivalent(found, soc_descriptor));
}

TEST(SimulatorInfo, RtlDirectoryWithoutSocDescriptorShipsNone) {
    const SimulatorLayout simulator;

    EXPECT_EQ(find_simulator_soc_descriptor_path(simulator.directory()), "");
}

// Silicon, and a card a ttsim-qemu VM emulates, are both reached through the KMD: no simulator backs them.
TEST(SimulatorInfo, DeviceReachedThroughKmdHasNoSimulationBackend) {
    if (is_simulation_test() || PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "Needs a device reached through the KMD.";
    }
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    EXPECT_EQ(cluster->get_tt_device(mmio_chip)->get_simulation_backend(), std::nullopt);
}
