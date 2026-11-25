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
#include <string>
#include <system_error>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "simulation/simulation_backend.hpp"
#include "simulation/simulation_connector_server.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "tt-umd/cluster_descriptor.hpp"
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
// The host's backend follows from the path the same way, so it is left to the host to tell. The
// sockets are carried alongside so discover() doesn't scan the directory a second time; a second
// scan would also be a TOCTOU race, since a host that exited in between would leave the role at
// Client while discover() built zero devices from a now-empty listing.
struct Classification {
    SimulationConnector::Role role = SimulationConnector::Role::Host;
    // Client only: the live per-chip sockets found in the directory.
    std::map<ChipId, std::filesystem::path> sockets;
};

Classification classify(const std::filesystem::path& simulator_path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(simulator_path, ec) && simulator_path.extension() == ".so") {
        return {SimulationConnector::Role::Host, {}};
    }
    UMD_ASSERT(
        std::filesystem::is_directory(simulator_path, ec),
        error::RuntimeError,
        fmt::format("Simulator path is neither a .so file nor a directory: {}", simulator_path.string()));
    // A directory that holds per-chip simulation sockets means a host is already serving there, so
    // attach as a client; any other directory is an RTL build we host.
    const auto sockets = SimulationServerSocket::sockets_in_directory(simulator_path);
    if (sockets.empty()) {
        return {SimulationConnector::Role::Host, {}};
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
    return {SimulationConnector::Role::Client, std::move(live_sockets)};
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

    // Made before a directory is claimed, so that a build that cannot answer requests leaves nothing behind.
    std::map<ChipId, SimulationServerSocket::RequestHandler> request_handlers =
        simulation_backend().make_server_request_handlers(devices, cluster_descriptor, std::move(shutdown_handler));

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
    auto socket = impl->sockets.begin();
    for (const auto& [chip_id, device] : devices) {
        (*socket++)->serve(std::move(request_handlers.at(chip_id)));
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
    const Classification classification = classify(options.simulator_directory);
    if (classification.role == Role::Client) {
        return simulation_backend().discover_as_client(options.simulator_directory, classification.sockets);
    }
    return simulation_backend().discover_as_host(options);
}

}  // namespace tt::umd
