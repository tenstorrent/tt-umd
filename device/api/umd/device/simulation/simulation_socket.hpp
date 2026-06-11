// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Owns the per-chip UNIX domain socket that exposes one simulation device on disk
// ("the card"). One socket per simulated device.
//
// Binding the socket is the act of surfacing the device; a client proves liveness
// by connecting to it. On construction it reclaims a stale socket left by a dead
// owner. Whether a live owner already holds the path is the host-vs-client arbiter:
// the throwing constructor refuses in that case, while try_create() returns nullptr so
// the caller (SimulationTopologyDiscovery) can attach as a client instead. On
// destruction it closes the socket and removes the file.
//
// It does not yet handle client requests: connections are accepted and dropped.
//
// Note: distinct from SimulationHost, which is the (nng) RTL transport over which the
// simulator process connects back into UMD.
class SimulationSocket {
public:
    // Binds and listens; throws if a live owner already holds the path.
    explicit SimulationSocket(const std::filesystem::path& socket_path);
    ~SimulationSocket();

    SimulationSocket(const SimulationSocket&) = delete;
    SimulationSocket& operator=(const SimulationSocket&) = delete;

    // Binds and listens, reclaiming a stale socket. Returns nullptr if a *live* owner already
    // holds the path (so the caller can attach as a client). Throws on real socket errors.
    static std::unique_ptr<SimulationSocket> try_create(const std::filesystem::path& socket_path);

    const std::filesystem::path& socket_path() const { return socket_path_; }

    // Deterministic per-chip socket path (the naming contract a client connects to):
    // <dir>/tt-umd-sim-<chip_id>.sock, where <dir> is $TT_UMD_SIM_SOCKET_DIR if set,
    // otherwise the system temp directory. No uid in the name: one shared socket per chip
    // that any user may attach to.
    static std::filesystem::path default_socket_path(ChipId chip_id = 0);

private:
    struct DeferBind {};

    SimulationSocket(const std::filesystem::path& socket_path, DeferBind);

    // Binds + listens + starts the accept loop. Returns false if a live owner holds the path;
    // throws on real socket errors.
    bool bind_and_listen();

    void accept_loop();

    std::filesystem::path socket_path_;
    int listen_fd_ = -1;
    int shutdown_pipe_[2] = {-1, -1};
    std::thread accept_thread_;
    std::atomic<bool> running_{false};
    // True only after a successful bind_and_listen(); gates teardown so a never-bound
    // object never removes a live owner's socket file.
    bool bound_ = false;
};

}  // namespace tt::umd
