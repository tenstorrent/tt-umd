// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>

namespace tt::umd {

class TTDevice;

/**
 * Creates a simulation TTDevice from a simulator path.
 * If the path ends with ".so", creates a TTSimTTDevice (functional simulator).
 * Otherwise, creates an RtlSimulationTTDevice (RTL simulator).
 *
 * @param simulator_path Path to the simulator binary (.so) or RTL simulator directory
 * @param num_host_mem_channels Number of host memory channels (default: 0)
 * @param copy_sim_binary If true, copy the simulator binary to memory (TTSim only, default: false)
 * @return A unique_ptr to the created TTDevice
 */
std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels = 0, bool copy_sim_binary = false);

/**
 * Returns the path to the soc_descriptor.yaml for a given simulator path.
 * If the path ends with ".so" (functional simulator), the descriptor is next to the binary.
 * Otherwise (RTL simulator directory), the descriptor is inside the directory.
 */
std::filesystem::path get_simulation_soc_descriptor_path(const std::filesystem::path &simulator_path);

}  // namespace tt::umd
