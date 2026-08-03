// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_connector.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <map>
#include <memory>
#include <system_error>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

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
enum class PathKind { HOST_TTSIM, HOST_RTL, CLIENT };

// The path kind and, for CLIENT, the sockets found in the directory -- returned together so
// discover() doesn't scan the directory a second time. The second scan would also be a TOCTOU race:
// if the host exited between the two scans, the kind would still be CLIENT but discover() would
// build zero devices from a now-empty listing and return silently.
struct Classification {
    PathKind kind;
    std::map<ChipId, std::filesystem::path> sockets;
};

Classification classify(const std::filesystem::path& simulator_path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(simulator_path, ec) && simulator_path.extension() == ".so") {
        return {PathKind::HOST_TTSIM, {}};
    }
    UMD_ASSERT(
        std::filesystem::is_directory(simulator_path, ec),
        error::RuntimeError,
        fmt::format("Simulator path is neither a .so file nor a directory: {}", simulator_path.string()));
    // A directory that holds per-chip simulation sockets means a host is already serving there, so
    // attach as a client; any other directory is an RTL build we host.
    auto sockets = SimulationServerSocket::sockets_in_directory(simulator_path);
    if (sockets.empty()) {
        return {PathKind::HOST_RTL, {}};
    }
    return {PathKind::CLIENT, std::move(sockets)};
}

// Host path: bring up the in-process backend (the direct hot path). A null socket means serving is
// off, so the device stays a private in-process host; a non-null socket is adopted so clients can
// attach.
std::unique_ptr<TTDevice> make_host_device(
    PathKind role,
    const std::filesystem::path& simulator_directory,
    int num_host_mem_channels,
    std::unique_ptr<SimulationServerSocket> socket) {
    if (role == PathKind::HOST_TTSIM) {
        auto device = TTSimTTDevice::create(simulator_directory, num_host_mem_channels);
        if (socket) {
            device->adopt_socket(std::move(socket));
        }
        return device;
    }
    auto device = RtlSimulationTTDevice::create(simulator_directory, num_host_mem_channels);
    if (socket) {
        device->adopt_socket(std::move(socket));
    }
    return device;
}

// Client path: build the device class the host reports over the wire. A client runs no local
// backend, so the class mostly just names the device; the backend kind still comes from the host so
// the right class is instantiated (and so this stays correct if the classes diverge).
std::unique_ptr<TTDevice> make_client_device(
    ChipId chip_id, std::unique_ptr<SimulationClient> client, const SimulationServerDeviceInfo& info) {
    switch (info.backend_type) {
        case SimulationBackendType::TTSIM:
            return TTSimTTDevice::create_client(chip_id, std::move(client), info);
        case SimulationBackendType::RTL:
            return RtlSimulationTTDevice::create_client(chip_id, std::move(client), info);
    }
    // A value outside the enum means a corrupt/incompatible reply from the host; fail loudly rather
    // than silently defaulting to a backend class.
    UMD_THROW(
        error::RuntimeError,
        fmt::format("Host reported an unknown simulation backend kind ({})", static_cast<int>(info.backend_type)));
}

}  // namespace

SimulationConnector::Role SimulationConnector::role_for(const std::filesystem::path& simulator_directory) {
    // The two host backends collapse to Host for callers that only care host-vs-client.
    return classify(simulator_directory).kind == PathKind::CLIENT ? Role::Client : Role::Host;
}

std::filesystem::path SimulationConnector::allocate_server_directory() {
    return SimulationServerSocket::allocate_server_directory();
}

std::vector<SimulationServerInfo> SimulationConnector::list_servers() {
    // Each server owns a directory under the system temp dir (see allocate_server_directory);
    // scanning for those directories and the sockets in each yields every open server, and the
    // chips it serves, without connecting to any.
    std::vector<SimulationServerInfo> servers;
    for (const auto& [index, directory] : SimulationServerSocket::list_server_directories()) {
        servers.push_back({index, directory, SimulationServerSocket::sockets_in_directory(directory)});
    }
    return servers;
}

std::map<ChipId, std::unique_ptr<TTDevice>> SimulationConnector::discover(const SimulationConnectorOptions& options) {
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    const std::filesystem::path& simulator_path = options.simulator_directory;

    const Classification classification = classify(simulator_path);

    if (classification.kind == PathKind::CLIENT) {
        // Multi-chip: one client device per per-chip socket in the directory (enumerated by
        // classify()). Chip ids come from the socket names, so they match the host's. A failure on
        // one socket (a dead or wedged host) only skips that chip -- it must not abort attaching to
        // the healthy ones.
        for (const auto& [chip_id, socket_file] : classification.sockets) {
            try {
                auto client = std::make_unique<SimulationClient>(socket_file);
                // The connector owns the single GET_DEVICE_INFO fetch: it needs the backend type to
                // pick the device class, and passes the fetched identity into create_client so it is
                // not fetched again.
                const SimulationServerDeviceInfo info = fetch_device_info_from_host(*client);
                devices.emplace(chip_id, make_client_device(chip_id, std::move(client), info));
            } catch (const std::exception& e) {
                log_warning(
                    LogUMD, "Skipping simulation socket {} (chip {}): {}", socket_file.string(), chip_id, e.what());
            }
        }
        // Every socket was present but none answered -- surface it rather than returning an empty,
        // successful-looking cluster.
        UMD_ASSERT(
            !devices.empty(),
            error::RuntimeError,
            fmt::format("No reachable simulation hosts among the sockets in {}", simulator_path.string()));
        return devices;
    }

    // Host: single chip for now. Serving over sockets is opt-in: by default the host device stays
    // private in-process (the direct hot path). When requested, serve in a dedicated server
    // directory -- the caller's, or a fresh one -- so two hosts never collide even when they serve
    // the same chip id; create() throws if a live host already owns this chip's socket there.
    const ChipId chip_id = 0;
    std::unique_ptr<SimulationServerSocket> socket;
    if (options.serve_over_sockets) {
        const std::filesystem::path server_directory = options.server_directory.empty()
                                                           ? SimulationServerSocket::allocate_server_directory()
                                                           : options.server_directory;
        socket = SimulationServerSocket::create(SimulationServerSocket::default_socket_path(server_directory, chip_id));
    }
    devices.emplace(
        chip_id,
        make_host_device(classification.kind, simulator_path, options.num_host_mem_channels, std::move(socket)));
    return devices;
}

}  // namespace tt::umd
