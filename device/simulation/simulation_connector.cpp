// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_connector.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <map>
#include <memory>
#include <system_error>
#include <utility>

#include "simulation/simulation_server_socket.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_device_identity.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// The role a UMD process takes for a given simulator_path -- decided purely from what the path is,
// with no socket bind-race:
//   - a ".so" file                 -> host running the TTSim backend for that library;
//   - a directory holding per-chip  -> client: a host already serves there, so attach to each
//     simulation sockets               socket in it (one device per socket), sourcing device
//                                      identity from the host over the wire;
//   - any other directory          -> host running the RTL backend from that build directory.
enum class Role { HostTTSim, HostRtl, Client };

Role classify(const std::filesystem::path& simulator_path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(simulator_path, ec) && simulator_path.extension() == ".so") {
        return Role::HostTTSim;
    }
    UMD_ASSERT(
        std::filesystem::is_directory(simulator_path, ec),
        error::RuntimeError,
        fmt::format("Simulator path is neither a .so file nor a directory: {}", simulator_path.string()));
    // A directory that holds per-chip simulation sockets means a host is already serving there, so
    // attach as a client; any other directory is an RTL build we host.
    return SimulationServerSocket::sockets_in_directory(simulator_path).empty() ? Role::HostRtl : Role::Client;
}

// Host path: bring up the in-process backend (the direct hot path) and hand it the socket to serve.
std::unique_ptr<TTDevice> make_host_device(
    Role role,
    const std::filesystem::path& simulator_directory,
    int num_host_mem_channels,
    std::unique_ptr<SimulationServerSocket> socket) {
    if (role == Role::HostTTSim) {
        auto device = TTSimTTDevice::create(simulator_directory, num_host_mem_channels);
        device->adopt_socket(std::move(socket));
        return device;
    }
    auto device = RtlSimulationTTDevice::create(simulator_directory, num_host_mem_channels);
    device->adopt_socket(std::move(socket));
    return device;
}

// Client path: build the device class the host reports over the wire. A client runs no local
// backend, so the class mostly just names the device; the backend kind still comes from the host so
// the right class is instantiated (and so this stays correct if the classes diverge).
std::unique_ptr<TTDevice> make_client_device(
    ChipId chip_id, std::unique_ptr<SimulationClient> client, const SimulationServerDeviceInfo& info) {
    switch (info.backend_type) {
        case SimulationBackendType::TTSim:
            return TTSimTTDevice::create_client(chip_id, std::move(client), info);
        case SimulationBackendType::Rtl:
            return RtlSimulationTTDevice::create_client(chip_id, std::move(client), info);
    }
    // A value outside the enum means a corrupt/incompatible reply from the host; fail loudly rather
    // than silently defaulting to a backend class.
    UMD_THROW(
        error::RuntimeError,
        fmt::format("Host reported an unknown simulation backend kind ({})", static_cast<int>(info.backend_type)));
}

}  // namespace

std::map<ChipId, std::unique_ptr<TTDevice>> SimulationConnector::discover(const SimulationConnectorOptions& options) {
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    const std::filesystem::path& simulator_path = options.simulator_directory;

    const Role role = classify(simulator_path);

    if (role == Role::Client) {
        // Multi-chip: one client device per per-chip socket in the directory. Chip ids come from
        // the socket names, so they match the host's.
        for (const auto& [chip_id, socket_file] : SimulationServerSocket::sockets_in_directory(simulator_path)) {
            auto client = std::make_unique<SimulationClient>(socket_file);
            // The connector owns the single GET_DEVICE_INFO fetch: it needs the backend kind to
            // pick the device class, and passes the fetched identity into create_client so it is
            // not fetched again.
            const SimulationServerDeviceInfo info = fetch_device_info_from_host(*client);
            devices.emplace(chip_id, make_client_device(chip_id, std::move(client), info));
        }
        return devices;
    }

    // Host: single chip for now. Claim the per-chip socket -- create() throws if a live host
    // already owns it (two hosts cannot serve the same chip) -- and serve it.
    const ChipId chip_id = 0;
    auto socket = SimulationServerSocket::create(SimulationServerSocket::default_socket_path(chip_id));
    devices.emplace(chip_id, make_host_device(role, simulator_path, options.num_host_mem_channels, std::move(socket)));
    return devices;
}

}  // namespace tt::umd
