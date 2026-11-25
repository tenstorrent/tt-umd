// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Topology discovery of a simulator that declares no topology and has none to enumerate: a Quasar TTSim image, or an
// RTL simulator build without an ip_layout.yaml. Discovery gives it one chip, chip 0. The suite runs when
// TT_UMD_SIMULATOR names such a Quasar simulator and skips otherwise; TTSimDeclaredQuasar declares a topology beside a
// copy of the Quasar TTSim image instead. RtlBuildTargetDevices needs no simulator: the
// ids it selects are refused before anything is launched.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/simulation_discovery.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::chips_of;
using tt::umd::test_utils::simulation_discovery_options;
using tt::umd::test_utils::SimulatorLayout;

class OneChipSimulatorTopology : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::exists(simulator_path)) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a simulator.";
        }
        simulator_ = simulator_path;
        is_ttsim_ = std::filesystem::is_regular_file(simulator_);
        const std::filesystem::path beside = is_ttsim_ ? simulator_.parent_path() : simulator_;
        if (std::filesystem::exists(beside / "cluster_descriptor.yaml") ||
            std::filesystem::exists(beside / "ip_layout.yaml")) {
            GTEST_SKIP() << "The simulator declares its topology.";
        }
        if (get_simulator_arch(simulator_) != tt::ARCH::QUASAR) {
            GTEST_SKIP() << "Only Quasar simulators are discovered as one chip without a declared topology.";
        }
    }

    std::filesystem::path simulator_;
    bool is_ttsim_ = false;
};

TEST_F(OneChipSimulatorTopology, DiscoveryFindsQuasarChipZero) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(simulator_));

    const auto chips = cluster_desc->get_all_chips();
    EXPECT_EQ(std::set<ChipId>(chips.begin(), chips.end()), (std::set<ChipId>{0}));
    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
    EXPECT_EQ(cluster_desc->get_arch(0), tt::ARCH::QUASAR);
}

TEST_F(OneChipSimulatorTopology, NocTranslationIsOnForTTSimAndOffForRtl) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(simulator_));

    // The device is returned as built, so its own SoC descriptor agrees with the cluster descriptor.
    if (is_ttsim_) {
        EXPECT_TRUE(cluster_desc->get_noc_translation_table_en().at(0));
        EXPECT_TRUE(devices.at(0)->get_soc_descriptor().noc_translation_enabled);
    } else {
        EXPECT_FALSE(cluster_desc->get_noc_translation_table_en().at(0));
        EXPECT_FALSE(devices.at(0)->get_soc_descriptor().noc_translation_enabled);
    }
}

// These simulators model no firmware to initialize the device from, so it keeps the SoC descriptor discovery built it
// from: here quasar_simulation_1x3.yaml, with one Tensix core.
TEST_F(OneChipSimulatorTopology, DeviceKeepsTheSocDescriptorItWasBuiltFrom) {
    TopologyDiscoveryOptions options = simulation_discovery_options(simulator_);
    options.soc_descriptor_path = test_utils::get_soc_desc_path("quasar_simulation_1x3.yaml");

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(devices.at(0)->get_soc_descriptor().get_cores(CoreType::TENSIX).size(), 1u);
}

TEST_F(OneChipSimulatorTopology, SocDescriptorOfAnotherArchIsRefused) {
    TopologyDiscoveryOptions options = simulation_discovery_options(simulator_);
    options.soc_descriptor_path = test_utils::get_soc_desc_path("wormhole_b0_8x10.yaml");

    EXPECT_THROW(TopologyDiscovery::discover(options), error::UmdException<error::RuntimeError>);
}

TEST_F(OneChipSimulatorTopology, DiscoveryStampsTheCallersClusterId) {
    TopologyDiscoveryOptions options = simulation_discovery_options(simulator_);
    options.cluster_id = "one-chip.cluster_1";

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(cluster_desc->get_cluster_id(), std::optional<std::string>("one-chip.cluster_1"));
}

TEST_F(OneChipSimulatorTopology, IllegalClusterIdIsRefused) {
    TopologyDiscoveryOptions options = simulation_discovery_options(simulator_);
    options.cluster_id = "not a cluster id!";

    EXPECT_ANY_THROW(TopologyDiscovery::discover(options));
}

// tt-metal opens RTL and Quasar simulators with target_devices {0}.
TEST_F(OneChipSimulatorTopology, TargetingChipZeroDiscoversIt) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(simulator_, {0}));

    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

TEST_F(OneChipSimulatorTopology, UnknownTargetDeviceIsRefused) {
    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(simulator_, {1})),
        error::UmdException<error::RuntimeError>);
}

TEST_F(OneChipSimulatorTopology, ClusterOpensTheOneChip) {
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_;
    Cluster cluster(options);

    EXPECT_EQ(cluster.get_target_device_ids(), (std::set<ChipId>{0}));
}

// A copy of the Quasar TTSim image with a one-chip topology declared beside it: a declaration takes the place of the
// one-chip fallback for Quasar as for any architecture.
class TTSimDeclaredQuasar : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path) ||
            get_simulator_arch(simulator_path) != tt::ARCH::QUASAR) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a Quasar TTSim library.";
        }
        layout_.add_cluster_descriptor(R"(arch:
  0: quasar
chips: {}
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 0}
)");
        layout_.add_soc_descriptor("quasar_32_arch.yaml");
        image_ = layout_.add_ttsim_library(simulator_path);
    }

    SimulatorLayout layout_;
    std::filesystem::path image_;
};

TEST_F(TTSimDeclaredQuasar, DiscoveryReturnsTheDeclaredDescriptor) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(image_));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550000}}));
    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

// An RTL simulator build laid out in a scratch directory, of which every test selects an id that discovery refuses.
// Its run.sh exits at once, so a launch does not fail the test by itself: a test that got as far as launching hangs
// waiting for the simulator's ack, until the caller's timeout, rather than passing (without a run.sh the launch
// would throw, which these tests would take for the refusal).
class RtlBuildTargetDevices : public ::testing::Test {
protected:
    void SetUp() override {
        layout_.add_soc_descriptor("quasar_simulation_1x3.yaml");
        const std::filesystem::path run_script = layout_.directory() / "run.sh";
        std::ofstream(run_script) << "#!/bin/sh\nexit 1\n";
        std::filesystem::permissions(run_script, std::filesystem::perms::owner_all);
    }

    // Two devices, each on its own access point, both described by the build's SoC descriptor.
    void add_two_device_ip_layout() const {
        std::ofstream(layout_.directory() / "ip_layout.yaml") << R"(access_points:
  - host_access: col0
    ip_block: 0
  - host_access: col1
    ip_block: 0
devices:
  - access_points: [0]
    soc_descriptor: soc_descriptor.yaml
  - access_points: [1]
    soc_descriptor: soc_descriptor.yaml
)";
    }

    SimulatorLayout layout_;
};

TEST_F(RtlBuildTargetDevices, UnknownChipOfABuildWithoutIpLayoutIsRefused) {
    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(layout_.directory(), {1})),
        error::UmdException<error::RuntimeError>);
}

TEST_F(RtlBuildTargetDevices, UnknownDeviceOfAnIpLayoutIsRefused) {
    add_two_device_ip_layout();

    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(layout_.directory(), {2})),
        error::UmdException<error::RuntimeError>);
}
