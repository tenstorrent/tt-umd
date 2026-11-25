// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The simulation connector and device factory in a build of UMD without simulation support: every call that needs a
// simulator is refused. The server-directory calls, which work in every build, are tested in
// test_simulation_server_directory.cpp.

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/simulation_socket_test_utils.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/tt_device/simulation_device_factory.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::ScopedServerDirectory;
using tt::umd::test_utils::SimulatorLayout;

// Even a simulator that ships its SoC descriptor is refused: this build cannot run any simulator.
TEST(SimulationConnectorWithoutSimulation, DiscoverNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("wormhole_b0_8x10.yaml");
    SimulationConnectorOptions options;
    options.simulator_directory = simulator.add_ttsim_library();

    EXPECT_THROW(SimulationConnector::discover(options), error::UmdException<error::RuntimeError>);
}

TEST(SimulationConnectorWithoutSimulation, CreateSimulationTTDeviceNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("wormhole_b0_8x10.yaml");

    EXPECT_THROW(create_simulation_tt_device(simulator.add_ttsim_library()), error::UmdException<error::RuntimeError>);
}

TEST(SimulationConnectorWithoutSimulation, CreateLocalSimulationTTDevicesNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("wormhole_b0_8x10.yaml");

    EXPECT_THROW(
        create_local_simulation_tt_devices(simulator.add_ttsim_library()), error::UmdException<error::RuntimeError>);
}

// The overload a chip is created through when its SoC descriptor is already known.
TEST(SimulationConnectorWithoutSimulation, CreateSimulationTTDeviceForAChipNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::get_soc_desc_path("wormhole_b0_8x10.yaml")));

    EXPECT_THROW(
        create_simulation_tt_device(simulator.add_ttsim_library(), soc_descriptor, 0, 1),
        error::UmdException<error::RuntimeError>);
}

// Any TTDevice can be served, so silicon devices are refused for the build alone.
TEST(SimulationConnectorWithoutSimulation, ServeNeedsSimulationSupport) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "Needs a Tenstorrent card to serve.";
    }
    const auto [cluster_descriptor, owned_devices] = TopologyDiscovery::discover();
    std::map<tt::ChipId, TTDevice*> devices;
    for (const auto& [chip, device] : owned_devices) {
        devices.emplace(chip, device.get());
    }
    const ScopedServerDirectory directory(SimulationConnector::allocate_server_directory());

    EXPECT_THROW(
        SimulationConnector::serve(devices, *cluster_descriptor, directory.path()),
        error::UmdException<error::RuntimeError>);
    // Nothing is served after the refusal: no chip's socket was left in the directory.
    for (const auto& [chip, device] : devices) {
        EXPECT_FALSE(std::filesystem::exists(directory.path() / ("tt-umd-sim-" + std::to_string(chip) + ".sock")));
    }
}
