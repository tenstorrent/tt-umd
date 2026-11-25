// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The public simulator queries that need UMD built with simulation support: which architecture a simulator models, and
// which simulator backs a device.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::SimulatorLayout;

namespace {

// The simulator TT_UMD_SIMULATOR names, or nullptr when the test runs without one.
const char* simulator_under_test() { return std::getenv("TT_UMD_SIMULATOR"); }

}  // namespace

struct ShippedSocDescriptor {
    std::string name;
    tt::ARCH arch;
};

class SimulatorArchFromSocDescriptor : public ::testing::TestWithParam<ShippedSocDescriptor> {};

// A soc_descriptor.yaml beside the .so names the architecture; the placeholder .so is never run.
TEST_P(SimulatorArchFromSocDescriptor, TTSimLibraryReportsTheShippedArch) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor(GetParam().name);

    EXPECT_EQ(get_simulator_arch(simulator.add_ttsim_library()), GetParam().arch);
}

INSTANTIATE_TEST_SUITE_P(
    SimulatorInfo,
    SimulatorArchFromSocDescriptor,
    ::testing::Values(
        ShippedSocDescriptor{"wormhole_b0_8x10.yaml", tt::ARCH::WORMHOLE_B0},
        ShippedSocDescriptor{"blackhole_140_arch.yaml", tt::ARCH::BLACKHOLE},
        ShippedSocDescriptor{"quasar_32_arch.yaml", tt::ARCH::QUASAR}),
    [](const ::testing::TestParamInfo<ShippedSocDescriptor>& info) { return tt::arch_to_str(info.param.arch); });

TEST(SimulatorInfo, RtlDirectoryReportsTheShippedArch) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("quasar_32_arch.yaml");

    EXPECT_EQ(get_simulator_arch(simulator.directory()), tt::ARCH::QUASAR);
}

// An RTL build has no libttsim image to ask, so its soc_descriptor.yaml is the only source of its architecture.
TEST(SimulatorInfo, RtlDirectoryWithoutSocDescriptorHasNoKnownArch) {
    const SimulatorLayout simulator;

    EXPECT_THROW(get_simulator_arch(simulator.directory()), error::UmdException<error::RuntimeError>);
}

// With no soc_descriptor.yaml to read, the architecture has to come from the image, and this one is no TTSim image.
TEST(SimulatorInfo, TTSimLibraryThatCannotBeProbedHasNoKnownArch) {
    const SimulatorLayout simulator;

    EXPECT_THROW(get_simulator_arch(simulator.add_ttsim_library()), error::UmdException<error::RuntimeError>);
}

// The tests below that ask a real image come before any test that brings a simulator up: an image without a
// soc_descriptor.yaml is run to answer, which the contract allows only before a simulator runs in this process. CI
// runs this suite in its own process per image for the same reason.

// TT_UMD_SIM_EXPECTED_ARCH is the architecture the image under test models, which only the job staging the image
// knows; unset, the test has no expected value and skips.
TEST(SimulatorInfo, SimulatorArchIsTheArchTheImageModels) {
    const char* simulator_path = simulator_under_test();
    const char* expected_arch = std::getenv("TT_UMD_SIM_EXPECTED_ARCH");
    if (simulator_path == nullptr || expected_arch == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR or TT_UMD_SIM_EXPECTED_ARCH is not set.";
    }

    EXPECT_EQ(get_simulator_arch(simulator_path), tt::arch_from_str(expected_arch));
}

// A Quasar image is described only by the soc_descriptor.yaml shipped beside it; the same image without one names a
// device UMD has no built-in SoC descriptor for.
TEST(SimulatorInfo, QuasarImageWithoutSocDescriptorHasNoKnownArch) {
    const char* simulator_path = simulator_under_test();
    if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path) ||
        get_simulator_arch(simulator_path) != tt::ARCH::QUASAR) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a Quasar TTSim library.";
    }
    const SimulatorLayout simulator;

    EXPECT_THROW(
        get_simulator_arch(simulator.add_ttsim_library(simulator_path)), error::UmdException<error::RuntimeError>);
}

// Once answered, the architecture stays available while the simulator it describes runs.
TEST(SimulatorInfo, SimulatorArchIsKeptWhileTheSimulatorRuns) {
    const char* simulator_path = simulator_under_test();
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }
    const tt::ARCH simulator_arch = get_simulator_arch(simulator_path);

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    EXPECT_EQ(get_simulator_arch(simulator_path), simulator_arch);
}

TEST(SimulatorInfo, TTSimDeviceIsBackedByTTSim) {
    const char* simulator_path = simulator_under_test();
    if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path)) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
    }
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    EXPECT_EQ(get_simulation_backend(*cluster->get_tt_device(0)), SimulationBackendType::TTSIM);
}

// A remote chip has no simulator of its own: it is reached over Ethernet through a chip the simulator backs.
TEST(SimulatorInfo, RemoteTTSimDeviceIsBackedByTTSim) {
    const char* simulator_path = simulator_under_test();
    if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path)) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
    }
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    const std::set<ChipId> remote_chips = cluster->get_target_remote_device_ids();
    if (remote_chips.empty()) {
        GTEST_SKIP() << "The simulator models no remote chip.";
    }

    EXPECT_EQ(get_simulation_backend(*cluster->get_tt_device(*remote_chips.begin())), SimulationBackendType::TTSIM);
}

TEST(SimulatorInfo, RtlDeviceIsBackedByRtl) {
    const char* simulator_path = simulator_under_test();
    if (simulator_path == nullptr || !std::filesystem::is_directory(simulator_path)) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name an RTL simulator directory.";
    }
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    EXPECT_EQ(get_simulation_backend(*cluster->get_tt_device(0)), SimulationBackendType::RTL);
}

// A device attached to a simulation server reports the simulator the server runs, not a backend of its own.
TEST(SimulatorInfo, SimulationServerClientDeviceIsBackedByTheServersSimulator) {
    const char* simulator_path = simulator_under_test();
    if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path)) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
    }
    const std::filesystem::path server_directory = SimulationConnector::allocate_server_directory();
    {
        SimulationConnectorOptions host_options;
        host_options.simulator_directory = simulator_path;
        host_options.serve_over_sockets = true;
        host_options.server_directory = server_directory;
        const SimulationConnector::Result host = SimulationConnector::discover(host_options);

        SimulationConnectorOptions client_options;
        client_options.simulator_directory = server_directory;
        const SimulationConnector::Result client = SimulationConnector::discover(client_options);

        EXPECT_EQ(get_simulation_backend(*client.devices.at(0)), SimulationBackendType::TTSIM);
    }
    std::filesystem::remove_all(server_directory);
}
