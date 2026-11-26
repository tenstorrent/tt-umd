// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

// Code that reaches the Cluster and Chip API through this header keeps compiling until it includes cluster.hpp itself.
#include "tt-umd-workload/cluster.hpp"  // IWYU pragma: export

namespace tt::umd {

// Holds the simulator path query tt-metal still calls. A simulation Cluster's chips are LocalChip and RemoteChip, and
// TTDevice::get_simulation_backend tells what simulates a device.
class [[deprecated("Use find_simulator_soc_descriptor_path (tt-umd/simulation/simulator_info.hpp).")]] SimulationChip {
public:
    SimulationChip() = delete;

    // Returns the path where the simulator's soc_descriptor.yaml would live, even when the simulator ships none
    // (find_simulator_soc_descriptor_path returns an empty string then).
    [[deprecated("Use find_simulator_soc_descriptor_path (tt-umd/simulation/simulator_info.hpp).")]] static std::string
    get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path);
};

}  // namespace tt::umd
