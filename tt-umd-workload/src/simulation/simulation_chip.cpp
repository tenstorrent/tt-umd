// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd-workload/simulation/simulation_chip.hpp"

#include <filesystem>
#include <string>

#include "simulation/simulator_paths.hpp"

namespace tt::umd {

std::string SimulationChip::get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path) {
    return get_simulator_soc_descriptor_path(simulator_path);
}

}  // namespace tt::umd
