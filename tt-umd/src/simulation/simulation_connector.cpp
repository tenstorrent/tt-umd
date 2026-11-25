// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/simulation/simulation_connector.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

#include "simulation/simulation_backend.hpp"
#include "simulation/simulation_connector_server.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/simulation/simulation_client.hpp"
#include "tt-umd/simulation/simulation_device_identity.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/tt_device/rtl_simulation_tt_device.hpp"
#include "tt-umd/tt_device/simulation_tt_device.hpp"
#include "tt-umd/tt_device/tt_sim_tt_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// What a given simulator_path means, decided purely from what the path is, with no socket
// bind-race:
//   - a ".so" file                 -> host, running the TTSim backend for that library;
//   - a directory holding per-chip  -> client: a host already serves there, so attach to each
//     simulation sockets               socket in it (one device per socket), sourcing device
//                                      identity from the host over the wire;
//   - any other directory          -> host, running the RTL backend from that build directory.
//
// Role and backend are orthogonal -- a client can attach to either backend -- so they are kept as
// separate fields rather than collapsed into one enum. The sockets are carried alongside so
// discover() doesn't scan the directory a second time; a second scan would also be a TOCTOU race,
// since a host that exited in between would leave the role at Client while discover() built zero
// devices from a now-empty listing and returned silently.
struct Classification {
    SimulationConnector::Role role = SimulationConnector::Role::Host;
    // Which backend this process would host. Empty for a client: a client runs no local backend,
    // and takes the host's backend_type from the device info it fetches over the wire (see
    // make_client_device).
    std::optional<SimulationBackendType> backend;
    // Client only: the per-chip sockets found in the directory.
    std::map<ChipId, std::filesystem::path> sockets;
};

Classification classify(const std::filesystem::path& simulator_path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(simulator_path, ec) && simulator_path.extension() == ".so") {
        return {SimulationConnector::Role::Host, SimulationBackendType::TTSIM, {}};
    }
    UMD_ASSERT(
        std::filesystem::is_directory(simulator_path, ec),
        error::RuntimeError,
        fmt::format("Simulator path is neither a .so file nor a directory: {}", simulator_path.string()));
    // A directory that holds per-chip simulation sockets means a host is already serving there, so
    // attach as a client; any other directory is an RTL build we host.
    const auto sockets = SimulationServerSocket::sockets_in_directory(simulator_path);
    if (sockets.empty()) {
        return {SimulationConnector::Role::Host, SimulationBackendType::RTL, {}};
    }
    // A socket file on disk only proves someone bound the path once, not that anyone is still
    // serving it: a crashed host leaves one behind that is indistinguishable by stat(). Count only
    // the sockets with a live listener, so a stale one neither drags a healthy directory's chip
    // list out of shape nor sends discover() off to connect to nothing.
    std::map<ChipId, std::filesystem::path> live_sockets;
    for (const auto& [chip_id, socket_path] : sockets) {
        if (SimulationServerSocket::is_live(socket_path)) {
            live_sockets.emplace(chip_id, socket_path);
        } else {
            log_warning(
                LogUMD,
                "Ignoring simulation socket {} (chip {}): no host is serving it.",
                socket_path.string(),
                chip_id);
        }
    }
    // Every socket is stale: the directory belonged to a host that is gone. Say so, rather than
    // falling through to hosting an RTL build out of a server directory -- which would fail later
    // with an unrelated message. `sim_server prune` clears these out.
    UMD_ASSERT(
        !live_sockets.empty(),
        error::RuntimeError,
        fmt::format(
            "The {} simulation socket(s) in {} are stale -- the host that served them is gone. Run `sim_server "
            "prune` to clear them, or point at a simulator to host one.",
            sockets.size(),
            simulator_path.string()));
    return {SimulationConnector::Role::Client, std::nullopt, std::move(live_sockets)};
}

// A server is gone when it published sockets and not one of them still has a listener: the host
// that bound them died without tearing them down. A directory with no socket at all is not gone --
// it reads exactly like a host that has claimed its directory and not bound yet -- so neither the
// listing nor the sweep may act on it.
bool is_server_gone(const SimulationServerInfo& server) {
    return !server.sockets.empty() &&
           std::none_of(server.sockets.begin(), server.sockets.end(), [](const auto& socket) {
               return SimulationServerSocket::is_live(socket.second);
           });
}

// Clears up after a server that is gone. Best-effort and quiet when it fails: simulation sockets
// are cross-user by design, and another user's directory cannot be removed from the temp
// directory, which every scan would otherwise report.
bool remove_server_directory(const SimulationServerInfo& server) {
    std::error_code ec;
    std::filesystem::remove_all(server.directory, ec);
    if (ec) {
        log_debug(LogUMD, "Could not remove {}: {}", server.directory.string(), ec.message());
        return false;
    }
    return true;
}

// Host path: bring up the in-process backend (the direct hot path). Serving, if requested, is
// started afterwards by the caller, once the host's topology is known -- it is part of what is served.
std::unique_ptr<SimulationTTDevice> make_host_device(
    SimulationBackendType backend, const std::filesystem::path& simulator_directory, int num_host_mem_channels) {
    if (backend == SimulationBackendType::TTSIM) {
        return TTSimTTDevice::create(simulator_directory, num_host_mem_channels);
    }
    return RtlSimulationTTDevice::create(simulator_directory, num_host_mem_channels);
}

// The topology the opened devices sit in. A simulator build, or a host serving one, may ship a
// cluster descriptor; when it does not, the chips that were actually opened are the whole topology,
// so a mock over exactly those says the same thing the rest of UMD says about a descriptor-less
// simulator.
std::shared_ptr<ClusterDescriptor> build_topology(
    const std::string& yaml,
    const std::map<ChipId, std::unique_ptr<TTDevice>>& devices,
    tt::ARCH arch,
    bool noc_translation_enabled) {
    if (!yaml.empty()) {
        return ClusterDescriptor::create_from_yaml_content(yaml);
    }
    std::unordered_set<ChipId> chip_ids;
    for (const auto& [chip_id, device] : devices) {
        chip_ids.insert(chip_id);
    }
    return ClusterDescriptor::create_mock_cluster(chip_ids, arch, noc_translation_enabled);
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

// What every socket of one Server answers alike. Shared by their request handlers, which outlive serve().
struct ServedTopology {
    std::string cluster_descriptor_yaml;
    std::function<void()> shutdown_handler;
};

// The identity a client builds its device from. A device reached through another chip runs on no simulator directly,
// so it is described without a simulator path.
SimulationServerDeviceInfo describe_served_device(const TTDevice& device) {
    const std::optional<SimulationBackend::DeviceConnection> connection =
        simulation_backend().get_device_connection(device);
    return describe_device(
        device.get_soc_descriptor(),
        get_simulation_backend(device).value_or(SimulationBackendType::TTSIM),
        connection.has_value() ? connection->simulator : std::filesystem::path{});
}

// Serves one socket request against device: decodes the wire request, runs it and encodes the reply. Runs on the
// socket's connection threads, one per attached client; device_mutex keeps their accesses to device one at a time.
std::vector<uint8_t> handle_request(
    TTDevice& device,
    std::mutex& device_mutex,
    const ServedTopology& served,
    const std::vector<uint8_t>& request_bytes) {
    const SimulationServerRequest request = decode_request(request_bytes);

    // Device info and the cluster descriptor have reply messages of their own.
    if (request.command == SimulationServerCommand::GET_DEVICE_INFO) {
        try {
            return encode(describe_served_device(device));
        } catch (const std::exception& e) {
            log_warning(LogUMD, "Simulation server failed to serve device info: {}", e.what());
            SimulationServerDeviceInfo info;
            info.status = -1;
            return encode(info);
        }
    }
    if (request.command == SimulationServerCommand::GET_CLUSTER_DESCRIPTOR) {
        SimulationServerClusterDescriptor cluster_descriptor;
        cluster_descriptor.status = 0;
        cluster_descriptor.yaml = served.cluster_descriptor_yaml;
        return encode(cluster_descriptor);
    }
    // The handler only signals the owner; the owner tears the Server down from its own thread, which joins this one
    // after the acknowledgement is sent.
    if (request.command == SimulationServerCommand::SHUTDOWN) {
        if (served.shutdown_handler) {
            served.shutdown_handler();
        }
        return encode(SimulationServerResponse{});
    }

    // The client already translated the coordinate, so it must not be translated again.
    const CoreCoord core{request.x, request.y, CoreType::UNSPECIFIED, CoordSystem::LITERAL};

    SimulationServerResponse response;
    try {
        const std::lock_guard<std::mutex> lock(device_mutex);
        switch (request.command) {
            case SimulationServerCommand::READ:
                response.data.resize(request.size);
                device.read_from_device(response.data.data(), core, request.address, request.size);
                break;
            case SimulationServerCommand::WRITE:
                UMD_ASSERT(
                    request.data.size() >= request.size,
                    error::RuntimeError,
                    fmt::format(
                        "Write request payload ({} bytes) is smaller than its size field ({})",
                        request.data.size(),
                        request.size));
                device.write_to_device(request.data.data(), core, request.address, request.size);
                break;
            default:
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("Unknown simulation server command {}", static_cast<int>(request.command)));
        }
    } catch (const std::exception& e) {
        // A failed access is the client's to handle, as a silicon access error would be; the connection stays up.
        log_warning(LogUMD, "Simulation server failed to serve a client request: {}", e.what());
        response.status = -1;
        response.data.clear();
    }
    return encode(response);
}

}  // namespace

std::unique_ptr<SimulationConnector::Server> SimulationConnector::serve(
    const std::map<ChipId, TTDevice*>& devices,
    const ClusterDescriptor& cluster_descriptor,
    const std::filesystem::path& server_directory,
    std::function<void()> shutdown_handler) {
    UMD_ASSERT(!devices.empty(), error::RuntimeError, "Serving simulation devices needs at least one device.");
    for (const auto& [chip_id, device] : devices) {
        UMD_ASSERT(
            device != nullptr, error::RuntimeError, fmt::format("The device to serve as chip {} is null.", chip_id));
        UMD_ASSERT(
            cluster_descriptor.get_all_chips().count(chip_id) != 0,
            error::RuntimeError,
            fmt::format("Chip {} is not a chip of the cluster descriptor it is served with.", chip_id));
    }

    auto impl = std::make_unique<Server::Impl>();
    impl->server_directory =
        server_directory.empty() ? SimulationServerSocket::allocate_server_directory() : server_directory;
    // Every socket is bound before any serves, so a chip a live server already serves there fails the call before a
    // client of this one is answered; the sockets bound until then are removed as the call unwinds.
    for (const auto& [chip_id, device] : devices) {
        const std::filesystem::path socket_path =
            SimulationServerSocket::default_socket_path(impl->server_directory, chip_id);
        impl->sockets.push_back(SimulationServerSocket::create(socket_path));
        impl->socket_paths.emplace(chip_id, socket_path);
    }

    const auto served = std::make_shared<const ServedTopology>(
        ServedTopology{cluster_descriptor.serialize(), std::move(shutdown_handler)});
    auto socket = impl->sockets.begin();
    for (const auto& [chip_id, device] : devices) {
        (*socket++)->serve([device = device, served, device_mutex = std::make_shared<std::mutex>()](
                               const std::vector<uint8_t>& request_bytes) {
            return handle_request(*device, *device_mutex, *served, request_bytes);
        });
    }
    log_info(LogUMD, "Serving {} simulation device(s) in {}", devices.size(), impl->server_directory.string());
    return std::unique_ptr<Server>(new Server(std::move(impl)));
}

SimulationConnector::Role SimulationConnector::role_for(const std::filesystem::path& simulator_directory) {
    return classify(simulator_directory).role;
}

std::filesystem::path SimulationConnector::allocate_server_directory() {
    return SimulationServerSocket::allocate_server_directory();
}

SimulationConnector::ServerScan SimulationConnector::scan_servers() {
    // The one enumeration of every server, so clearing up after hosts that are gone happens
    // wherever all servers are looked at rather than each caller remembering to ask. An operation
    // aimed at a directory the caller named must not be built on this: that directory is theirs to
    // be told the truth about, not to have swept out from under them (see classify()).
    ServerScan scan;
    for (const auto& [index, directory] : SimulationServerSocket::list_server_directories()) {
        SimulationServerInfo server{index, directory, SimulationServerSocket::sockets_in_directory(directory)};
        if (!is_server_gone(server)) {
            scan.live.push_back(std::move(server));
            continue;
        }
        // Asked again immediately before removing: the probe above and the removal are not one
        // operation, and a host may bind a stale socket in between (SimulationServerSocket::create
        // reclaims one). This narrows that window; only a claim on the directory itself would
        // close it.
        if (is_server_gone(server) && remove_server_directory(server)) {
            scan.removed.push_back(std::move(server));
        }
        // A server that is gone and could not be removed is in neither half: not a server anything
        // can attach to, and not ours to clear up.
    }
    return scan;
}

std::vector<SimulationServerInfo> SimulationConnector::list_servers() { return scan_servers().live; }

std::vector<SimulationServerInfo> SimulationConnector::prune_dead_servers() { return scan_servers().removed; }

SimulationConnector::Result SimulationConnector::discover(const SimulationConnectorOptions& options) {
    Result result;
    std::map<ChipId, std::unique_ptr<TTDevice>>& devices = result.devices;
    const std::filesystem::path& simulator_path = options.simulator_directory;

    const Classification classification = classify(simulator_path);

    if (classification.role == Role::Client) {
        // Taken from the first socket that answers, alongside the identity already fetched there:
        // one host serves the whole directory, so its topology describes every chip in it.
        std::string served_topology_yaml;
        bool noc_translation_enabled = false;
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
                // A server directory is claimed atomically by one host (see
                // allocate_server_directory), so every socket in it is served by one process
                // running one simulator: the first socket that answers describes the whole
                // directory. Warn rather than silently describing only the first if that ever
                // fails to hold -- a hand-assembled directory mixing two servers' sockets is the
                // only way it can, and it should not pass unnoticed.
                if (result.devices.empty()) {
                    result.connection.simulator = info.simulator_path;
                    result.connection.backend = info.backend_type;
                    result.connection.arch = static_cast<tt::ARCH>(info.arch);
                    noc_translation_enabled = info.noc_translation_enabled;
                    // Fetched here, while this client is still ours to send on: it is moved into
                    // the device below.
                    served_topology_yaml = fetch_cluster_descriptor_yaml(*client);
                } else if (
                    info.simulator_path != result.connection.simulator.string() ||
                    info.backend_type != result.connection.backend ||
                    static_cast<tt::ARCH>(info.arch) != result.connection.arch) {
                    log_warning(
                        LogUMD,
                        "Simulation socket {} (chip {}) reports a different simulation than the rest of {}: "
                        "{} ({}/{}) instead of {} ({}/{}). Reporting the first; the directory is serving more "
                        "than one host.",
                        socket_file.string(),
                        chip_id,
                        simulator_path.string(),
                        info.simulator_path,
                        arch_to_str(static_cast<tt::ARCH>(info.arch)),
                        info.backend_type == SimulationBackendType::TTSIM ? "ttsim" : "rtl",
                        result.connection.simulator.string(),
                        arch_to_str(result.connection.arch),
                        result.connection.backend == SimulationBackendType::TTSIM ? "ttsim" : "rtl");
                }
                devices.emplace(chip_id, make_client_device(chip_id, std::move(client), info));
                // Only once the device is in: make_client_device() throws on a backend kind this
                // build doesn't know, and the catch below skips the chip. Recording the socket
                // first would leave connection.sockets naming a chip that has no device.
                result.connection.sockets.emplace(chip_id, socket_file);
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
        result.connection.role = Role::Client;
        result.connection.server_directory = simulator_path;
        result.cluster_descriptor =
            build_topology(served_topology_yaml, devices, result.connection.arch, noc_translation_enabled);
        return result;
    }

    // Host: single chip for now. By default the host device stays private in-process (the direct hot path); serving
    // it is opt-in.
    constexpr ChipId chip_id = 0;
    // value() rather than operator*: classify() always sets a backend for the host role, and this
    // makes that invariant explicit instead of reading an empty optional if it ever stops holding.
    const SimulationBackendType backend = classification.backend.value();
    devices.emplace(chip_id, make_host_device(backend, simulator_path, options.num_host_mem_channels));

    result.connection.role = Role::Host;
    result.connection.simulator = simulator_path;
    result.connection.backend = backend;
    const SocDescriptor& soc_descriptor = devices.at(chip_id)->get_soc_descriptor();
    result.connection.arch = soc_descriptor.arch;
    // The host's topology is what the simulator build describes, never one a caller hands in: the
    // simulator was not configured from it, so it could name chips that have no device here.
    if (options.cluster_descriptor != nullptr) {
        log_warning(
            LogUMD,
            "Ignoring the cluster descriptor passed to the simulation host for {}; reporting the simulator's own "
            "topology instead.",
            simulator_path.string());
    }
    result.cluster_descriptor = build_topology(
        describe_cluster(simulator_path).yaml, devices, result.connection.arch, soc_descriptor.noc_translation_enabled);
    // Serve that same topology, so every client that attaches sees the cluster this host reports.
    if (options.serve_over_sockets) {
        result.server =
            serve({{chip_id, devices.at(chip_id).get()}}, *result.cluster_descriptor, options.server_directory);
        result.connection.server_directory = result.server->server_directory();
        result.connection.sockets = result.server->sockets();
    }
    return result;
}

}  // namespace tt::umd
