// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>

#include "tt-umd/topology/ip_layout.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class RtlSimIpLayout;
class TTDevice;

/**
 * Create the devices of an RTL simulator build with an ip_layout.yaml: one RtlSimulationTTDevice per
 * entry of @p ip_devices (chip id -> the layout's device), each on its device's socket of one shared
 * RtlSimSession, which is started first. Each device must have exactly one access point. With
 * @p launch_simulator false the session serves its sockets without launching run.sh, and another
 * process launches it.
 *
 * @param simulator_directory The RTL build directory, holding run.sh.
 * @param layout The build's ip_layout.yaml.
 * @param ip_devices The devices to open, keyed by the chip id each one gets.
 * @param num_host_mem_channels Host memory channels per device.
 * @param launch_simulator Whether this process launches run.sh.
 */
std::map<ChipId, std::unique_ptr<TTDevice>> create_rtl_sim_ip_layout_tt_devices(
    const std::filesystem::path &simulator_directory,
    const RtlSimIpLayout &layout,
    const std::map<ChipId, IpDeviceId> &ip_devices,
    int num_host_mem_channels = 0,
    bool launch_simulator = true);

}  // namespace tt::umd
