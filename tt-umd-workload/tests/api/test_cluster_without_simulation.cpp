// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Cluster and SimulationChip in a build of UMD without simulation support: choosing a simulator is refused with
// error::RuntimeError. The same calls with simulation support are tested against real simulators in
// tests/api/test_ttsim_topology_discovery.cpp and tests/api/test_sim_multichip.cpp.

#include <gtest/gtest.h>

#include <memory>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd-workload/simulation/simulation_chip.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::SimulatorLayout;

// Even a simulator that ships its SoC descriptor is refused: this build cannot run any simulator.
TEST(ClusterWithoutSimulation, SimulationClusterNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("wormhole_b0_8x10.yaml");
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator.add_ttsim_library();

    EXPECT_THROW(Cluster cluster(options), error::UmdException<error::RuntimeError>);
}

// A caller-supplied cluster descriptor does not get around the build: its chips still need a simulator.
TEST(ClusterWithoutSimulation, SimulationClusterWithCallerDescriptorNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("wormhole_b0_8x10.yaml");
    const std::unique_ptr<ClusterDescriptor> cluster_descriptor =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::WORMHOLE_B0, /*noc_translation_enabled=*/true);
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator.add_ttsim_library();
    options.cluster_descriptor = cluster_descriptor.get();

    EXPECT_THROW(Cluster cluster(options), error::UmdException<error::RuntimeError>);
}

TEST(ClusterWithoutSimulation, SimulationChipNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::get_soc_desc_path("wormhole_b0_8x10.yaml")));

    EXPECT_THROW(
        SimulationChip::create(simulator.add_ttsim_library(), soc_descriptor, /*chip_id=*/0, /*num_chips=*/1),
        error::UmdException<error::RuntimeError>);
}
