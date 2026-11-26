// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/simulation/simulator_info.hpp"

#include "simulation/simulation_backend.hpp"
#include "simulation/simulator_paths.hpp"

namespace tt::umd {

tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path) {
    return simulation_backend().get_simulator_arch(simulator_path);
}

std::string find_simulator_soc_descriptor_path(const std::filesystem::path& simulator_path) {
    std::string soc_descriptor_path = get_simulator_soc_descriptor_path(simulator_path);
    return std::filesystem::exists(soc_descriptor_path) ? soc_descriptor_path : "";
}

}  // namespace tt::umd
