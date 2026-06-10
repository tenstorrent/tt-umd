// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_topology_discovery.hpp"

#include <fmt/format.h>

#include <utility>

#include "umd/device/simulation/simulation_socket.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::map<ChipId, std::unique_ptr<TTDevice>> SimulationTopologyDiscovery::discover(
    const SimulationTopologyDiscoveryOptions& options) {
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;

    // Single-chip for now. Socket-first: try to claim the path; success => we are the host.
    const ChipId chip_id = 0;
    const std::filesystem::path socket_path = SimulationSocket::default_socket_path(chip_id);

    std::unique_ptr<SimulationSocket> socket = SimulationSocket::try_create(socket_path);
    if (socket == nullptr) {
        // A live host already serves this device. The client/attach path lands in a later PR.
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Attaching to an existing simulation host is not implemented yet; a live socket "
                "already exists at: {}",
                socket_path.string()));
    }

    // We are the host: bring up the backend (the direct in-process hot path), then serve clients
    // by dispatching their requests into that backend, and hand the device the socket to own.
    std::unique_ptr<TTSimTTDevice> device =
        TTSimTTDevice::create(options.simulator_directory, options.num_host_mem_channels);
    TTSimTTDevice* device_ptr = device.get();
    socket->start_serving(
        [device_ptr](const std::vector<uint8_t>& request) { return device_ptr->handle_request(request); });
    device->adopt_socket(std::move(socket));

    devices.emplace(chip_id, std::move(device));
    return devices;
}

}  // namespace tt::umd
