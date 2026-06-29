// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_connector.hpp"

#include <fmt/format.h>

#include <utility>

#include "simulation/simulation_server_socket.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::map<ChipId, std::unique_ptr<TTDevice>> SimulationConnector::discover(const SimulationConnectorOptions& options) {
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;

    // Single-chip for now. Socket-first: try to claim the path; success => we are the host.
    const ChipId chip_id = 0;
    const std::filesystem::path socket_path = SimulationServerSocket::default_socket_path(chip_id);

    // The backend (TTSim .so vs RTL directory) is chosen by the simulator path, mirroring the
    // simulation device factory; the host-vs-client role is chosen socket-first below.
    const bool is_ttsim = options.simulator_directory.extension() == ".so";

    std::unique_ptr<SimulationServerSocket> socket = SimulationServerSocket::try_create(socket_path);
    if (socket == nullptr) {
        // A live host already serves this device. The client/attach path lands in a later PR.
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Attaching to an existing simulation host is not implemented yet; a live socket "
                "already exists at: {}",
                socket_path.string()));
    }

    // We are the host: bring up the backend (the direct in-process hot path) and hand it the
    // socket to own and expose. The two backends don't share a common factory return type that
    // carries adopt_socket, so dispatch on the concrete device here.
    if (is_ttsim) {
        std::unique_ptr<TTSimTTDevice> device =
            TTSimTTDevice::create(options.simulator_directory, options.num_host_mem_channels);
        device->adopt_socket(std::move(socket));
        devices.emplace(chip_id, std::move(device));
    } else {
        std::unique_ptr<RtlSimulationTTDevice> device =
            RtlSimulationTTDevice::create(options.simulator_directory, options.num_host_mem_channels);
        device->adopt_socket(std::move(socket));
        devices.emplace(chip_id, std::move(device));
    }
    return devices;
}

}  // namespace tt::umd
