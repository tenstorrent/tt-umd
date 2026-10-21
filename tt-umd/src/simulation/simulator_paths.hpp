// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

namespace tt::umd {

// Simulator artifacts such as the descriptor files live beside the simulator: next to a TTSim .so, or inside an RTL
// simulator directory.
inline std::filesystem::path get_simulator_artifacts_dir(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? simulator_path.parent_path() : simulator_path;
}

inline std::string get_simulator_soc_descriptor_path(const std::filesystem::path& simulator_path) {
    return get_simulator_artifacts_dir(simulator_path) / "soc_descriptor.yaml";
}

inline std::string get_simulator_cluster_descriptor_path(const std::filesystem::path& simulator_path) {
    return get_simulator_artifacts_dir(simulator_path) / "cluster_descriptor.yaml";
}

}  // namespace tt::umd
