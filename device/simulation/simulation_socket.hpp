// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Manages a per-chip UNIX domain socket that represents a simulated device on disk
// ("the card"). One socket per simulated chip.
//
// The socket acts as a presence indicator: if a process can bind the path it becomes the
// host; if a live host already exists, try_create() returns nullptr so the caller
// (SimulationTopologyDiscovery) can attach as a client instead, while create() throws. Stale
// sockets left by crashed owners are automatically reclaimed. On destruction the host closes
// the socket and removes the file.
//
// It does not yet handle client requests: connections are accepted and dropped.
//
// Internal to the simulation subsystem (not part of the public UMD API). The asio transport
// is held behind a pImpl so this header stays free of asio (a private dependency of the
// library) -- see Impl in the .cpp.
//
// Note: distinct from SimulationHost, which is the (nng) RTL transport over which the
// simulator process connects back into UMD.
class SimulationSocket {
public:
    ~SimulationSocket();

    SimulationSocket(const SimulationSocket&) = delete;
    SimulationSocket& operator=(const SimulationSocket&) = delete;

    // Binds and listens, reclaiming a stale socket. Returns nullptr if a *live* owner already
    // holds the path (so the caller can attach as a client). Throws on real socket errors.
    static std::unique_ptr<SimulationSocket> try_create(const std::filesystem::path& socket_path);

    // Like try_create(), but throws (instead of returning nullptr) when a live owner already
    // holds the path. For callers that require ownership and have no client path to fall back to.
    static std::unique_ptr<SimulationSocket> create(const std::filesystem::path& socket_path);

    const std::filesystem::path& socket_path() const { return socket_path_; }

    // Deterministic per-chip socket path (the naming contract a client connects to):
    // <temp_dir>/tt-umd-sim-<chip_id>.sock, under the system temp directory. No uid in the
    // name: one shared socket per chip that any user may attach to.
    static std::filesystem::path default_socket_path(ChipId chip_id = 0);

private:
    // Non-throwing; only initializes members. Binding happens in bind_and_listen(), driven by
    // the try_create()/create() factories.
    explicit SimulationSocket(const std::filesystem::path& socket_path);

    // Binds + listens + starts the accept loop. Returns false if a live owner holds the path;
    // throws on real socket errors.
    bool bind_and_listen();

    // True if a listener is currently reachable at socket_path_.
    bool is_live();

    // Re-arms the async accept; connections carry no requests yet (owner-only), so each
    // accepted socket is dropped immediately.
    void do_accept();

    std::filesystem::path socket_path_;
    // True only after a successful bind_and_listen(); gates teardown so a never-bound
    // object never removes a live owner's socket file.
    bool bound_ = false;

    // Holds the asio transport (io_context, acceptor, accept-loop thread). Defined in the .cpp
    // so asio stays out of this header; owns the listen fd (RAII) and the shutdown handshake
    // (io_context::stop() instead of a self-pipe).
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::umd
