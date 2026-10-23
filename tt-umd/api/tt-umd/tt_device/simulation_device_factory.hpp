// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

#include "tt-umd/types/cluster_descriptor_types.hpp"

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
 * @return A unique_ptr to the created TTDevice
 */
std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels = 0);

/**
 * Same as above, but uses a caller-provided SocDescriptor and ChipId instead of deriving them
 * from the simulator path. num_chips is how many chips are opened over PCIe on the image; a TTSim
 * image has to expose at least that many endpoints unless it implements the multichip ABI.
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

/**
 * Enumerate a simulator image's host-visible endpoints and create one device per endpoint.
 *
 * The counterpart, for a backend with no OS device enumeration, of PCIDevice::enumerate_devices()
 * followed by create_tt_device() per device. The image reports how many chips it models, so the
 * caller does not have to be told by a cluster descriptor.
 *
 * @param simulator_path Path to the libttsim .so.
 * @param num_host_mem_channels Host memory channels per device.
 * @return One device per endpoint, keyed by chip id, in endpoint order.
 */
std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
    const std::filesystem::path &simulator_path, int num_host_mem_channels = 0);

}  // namespace tt::umd
