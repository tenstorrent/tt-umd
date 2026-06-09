// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class SocDescriptor;
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
 * Same as above, but uses a caller-provided SocDescriptor and ChipId instead of deriving them
 * from the simulator path. copy_sim_binary is derived from num_chips > 1 (needed when multiple
 * TTSim instances run in the same process).
 *
 * Intended for use from SimulationChip::create and Cluster::construct_chip_from_cluster, where
 * the SocDescriptor is already known.
 */
std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_directory,
    const SocDescriptor &soc_descriptor,
    ChipId chip_id,
    size_t num_chips,
    int num_host_mem_channels = 0);

}  // namespace tt::umd
