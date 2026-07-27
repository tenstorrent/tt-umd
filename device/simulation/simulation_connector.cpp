// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_connector.hpp"

#include <memory>
#include <utility>

#include "simulation/simulation_server_socket.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

namespace {

// Builds the device for one simulated chip. The backend (TTSim .so vs RTL directory) is chosen by
// the simulator path; the host-vs-client role is decided by who owns the socket:
//   - socket == nullptr: a live host already serves this chip, so attach as a client that reaches
//     it over the socket via SimulationClient (slim today -- just connect/disconnect; device-op
//     forwarding lands as the API grows). The client runs no backend, so the only backend-specific
//     bit is which device class describes it.
//   - socket != nullptr: we are the host; bring up the in-process backend (the direct hot path) and
//     hand it the socket to own and expose.
// The two backends don't share a common factory return type that carries adopt_socket, so dispatch
// on the concrete device here.
std::unique_ptr<TTDevice> create_simulation_device(
    const std::filesystem::path& simulator_directory,
    ChipId chip_id,
    int num_host_mem_channels,
    const std::filesystem::path& socket_path,
    std::unique_ptr<SimulationServerSocket> socket) {
    const bool is_ttsim = simulator_directory.extension() == ".so";

    if (socket == nullptr) {
        auto client = std::make_unique<SimulationClient>(socket_path);
        if (is_ttsim) {
            return TTSimTTDevice::create_client(chip_id, std::move(client));
        }
        return RtlSimulationTTDevice::create_client(chip_id, std::move(client));
    }

    if (is_ttsim) {
        auto device = TTSimTTDevice::create(simulator_directory, num_host_mem_channels);
        device->adopt_socket(std::move(socket));
        return device;
    }
    auto device = RtlSimulationTTDevice::create(simulator_directory, num_host_mem_channels);
    device->adopt_socket(std::move(socket));
    return device;
}

}  // namespace

std::map<ChipId, std::unique_ptr<TTDevice>> SimulationConnector::discover(const SimulationConnectorOptions& options) {
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;

    // Single-chip for now. Socket-first: try to claim the path; success => we are the host.
    const ChipId chip_id = 0;
    const std::filesystem::path socket_path = SimulationServerSocket::default_socket_path(chip_id);
    std::unique_ptr<SimulationServerSocket> socket = SimulationServerSocket::try_create(socket_path);

    devices.emplace(
        chip_id,
        create_simulation_device(
            options.simulator_directory, chip_id, options.num_host_mem_channels, socket_path, std::move(socket)));
    return devices;
}

}  // namespace tt::umd
