// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

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
    int num_host_mem_channels = 0,
    std::optional<uint32_t> image_endpoint_count = std::nullopt);

/**
 * Enumerate a simulator image's host-visible endpoints and create one device per endpoint.
 *
 * The counterpart, for a backend with no OS device enumeration, of PCIDevice::enumerate_devices()
 * followed by TTDevice::create() per device. The image reports how many chips it models, so the
 * caller does not have to be told by a cluster descriptor.
 *
 * @param simulator_path Path to the libttsim .so.
 * @param num_host_mem_channels Host memory channels per device.
 * @return One device per endpoint, keyed by chip id, in endpoint order.
 */
std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
    const std::filesystem::path &simulator_path, int num_host_mem_channels = 0);

/**
 * Create the devices of an RTL simulator build with an ip_layout.yaml: one RtlSimulationTTDevice per
 * entry of @p chips (chip id -> the layout's device id), each on its device's socket of one shared
 * RtlSimSession, which is started first. Each device must have exactly one access point. With
 * @p launch_simulator false the session serves its sockets without launching run.sh, and another
 * process launches it.
 *
 * @param simulator_directory The RTL build directory, holding ip_layout.yaml and run.sh.
 * @param chips The devices to open, keyed by the chip id each one gets.
 * @param num_host_mem_channels Host memory channels per device.
 * @param launch_simulator Whether this process launches run.sh.
 */
std::map<ChipId, std::unique_ptr<TTDevice>> create_rtl_sim_ip_layout_tt_devices(
    const std::filesystem::path &simulator_directory,
    const std::map<ChipId, uint32_t> &chips,
    int num_host_mem_channels = 0,
    bool launch_simulator = true);

}  // namespace tt::umd
