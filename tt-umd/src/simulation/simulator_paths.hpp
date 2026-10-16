// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

namespace tt::umd {

// Descriptor files live beside the simulator: next to a TTSim .so, or inside an RTL simulator directory.
inline std::string get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "soc_descriptor.yaml")
                                                 : (simulator_path / "soc_descriptor.yaml");
}

inline std::string get_cluster_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "cluster_descriptor.yaml")
                                                 : (simulator_path / "cluster_descriptor.yaml");
}

}  // namespace tt::umd
