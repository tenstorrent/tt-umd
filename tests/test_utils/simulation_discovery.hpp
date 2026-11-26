// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <unordered_set>

#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd::test_utils {

// Options that discover the chips of the simulator at simulator_path, or only target_devices of them.
inline TopologyDiscoveryOptions simulation_discovery_options(
    const std::filesystem::path& simulator_path, const std::unordered_set<ChipId>& target_devices = {}) {
    TopologyDiscoveryOptions options;
    options.simulation = SimulationDiscoveryOptions{};
    options.simulation->simulator_path = simulator_path.string();
    options.simulation->target_devices = target_devices;
    return options;
}

// Where the simulator at simulator_path declares its topology, whether or not it does: the cluster_descriptor.yaml
// beside a TTSim library, or inside an RTL simulator directory.
inline std::filesystem::path declared_cluster_descriptor_path(const std::filesystem::path& simulator_path) {
    const std::filesystem::path beside =
        std::filesystem::is_regular_file(simulator_path) ? simulator_path.parent_path() : simulator_path;
    return beside / "cluster_descriptor.yaml";
}

// The chip ids discovery opened a device for.
inline std::set<ChipId> chips_of(const std::map<ChipId, std::unique_ptr<TTDevice>>& devices) {
    std::set<ChipId> chips;
    for (const auto& [chip, _] : devices) {
        chips.insert(chip);
    }
    return chips;
}

}  // namespace tt::umd::test_utils
