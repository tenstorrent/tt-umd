// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Manages a per-chip UNIX domain socket that represents a simulated device on disk
// ("the card"). One socket per simulated chip, kept in a per-server directory
// (allocate_server_directory) so distinct hosts on the same machine never collide.
//
// The socket acts as a presence indicator: if a process can bind the path it becomes the
// host; if a live host already exists, try_create() returns nullptr so the caller
// (SimulationConnector) can attach as a client instead, while create() throws. Stale
// sockets left by crashed owners are automatically reclaimed. On destruction the host closes
// the socket and removes the file.
//
// Request handling is split from binding, so the owner can claim the socket first and begin
// serving only once its backend is ready (the handler dispatches into that backend, which does
// not exist at bind time -- see the host device's adopt_socket()):
//   - bind (try_create/create): the socket is bound and connectable -- pure presence/liveness --
//     but accepts nothing until serve() is called.
//   - serve(handler): starts the accept loop; each accepted connection is served on its own
//     thread, which reads one length-prefixed request at a time (see simulation_server_transport),
//     passes the opaque request bytes to the handler, and writes back the framed reply, until the
//     peer disconnects. The socket layer stays protocol-agnostic (it moves opaque payloads); the
//     handler owns encoding/decoding and dispatch. Because a client may share the host, the
//     handler must be safe to call from multiple connection threads.
//
// Internal to the simulation subsystem (not part of the public UMD API). The asio transport
// is held behind a pImpl so this header stays free of asio (a private dependency of the
// library) -- see Impl in the .cpp.
//
// Note: distinct from SimulationHost, which is the (nng) RTL transport over which the
// simulator process connects back into UMD.
class SimulationServerSocket {
public:
    // Serves a single connection: opaque request payload in, opaque reply payload out. Called on
    // a per-connection thread; must be thread-safe if more than one client attaches.
    using RequestHandler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

    ~SimulationServerSocket();

    SimulationServerSocket(const SimulationServerSocket&) = delete;
    SimulationServerSocket& operator=(const SimulationServerSocket&) = delete;

    // Binds and listens, reclaiming a stale socket. Returns nullptr if a *live* owner already
    // holds the path (so the caller can attach as a client). Throws on real socket errors. The
    // socket is presence-only (bound + connectable) until serve() installs a handler.
    static std::unique_ptr<SimulationServerSocket> try_create(const std::filesystem::path& socket_path);

    // Like try_create(), but throws (instead of returning nullptr) when a live owner already
    // holds the path. For callers that require ownership and have no client path to fall back to.
    static std::unique_ptr<SimulationServerSocket> create(const std::filesystem::path& socket_path);

    // Starts serving accepted connections through request_handler (see the class comment). Call
    // once, after the backend the handler dispatches into is ready. The handler is installed
    // before the accept loop starts, so it is set before any connection thread reads it.
    void serve(RequestHandler request_handler);

    const std::filesystem::path& socket_path() const { return socket_path_; }

    // Claims a fresh directory for one simulation server: <temp_dir>/tt-umd-sim-server-<index>,
    // for the lowest index not already taken. Each server owns its own directory so two hosts
    // never collide -- even when they serve overlapping chip ids, the sockets live in different
    // directories. The claim is atomic (create_directory fails if the name exists), so racing
    // hosts get distinct indices. Throws if no index is free. A client is then pointed at the
    // returned directory.
    static std::filesystem::path allocate_server_directory();

    // The simulation server directories present under the system temp dir: {index -> directory},
    // keyed and ordered by index. This is what a caller enumerating the machine's servers scans;
    // sockets_in_directory() then lists the chips each one serves.
    static std::map<int, std::filesystem::path> list_server_directories();

    // Inverse of allocate_server_directory()'s naming: pulls the index out of a directory *name*
    // (tt-umd-sim-server-<index>). Returns nullopt when the name doesn't match the convention.
    static std::optional<int> server_index_from_directory_path(const std::filesystem::path& directory);

    // Deterministic per-chip socket path inside a server directory (the naming contract a client
    // connects to): <server_directory>/tt-umd-sim-<chip_id>.sock. No uid in the name: one shared
    // socket per chip that any user may attach to.
    static std::filesystem::path default_socket_path(const std::filesystem::path& server_directory, ChipId chip_id = 0);

    // Inverse of default_socket_path()'s naming: pulls the chip id out of a socket file *name*
    // (tt-umd-sim-<chip_id>.sock). Returns nullopt when the name doesn't match the convention, so a
    // client enumerating a directory can pick out exactly the per-chip simulation sockets.
    static std::optional<ChipId> chip_id_from_socket_path(const std::filesystem::path& socket_path);

    // The per-chip simulation sockets present in a directory: {chip_id -> socket file}, keyed and
    // ordered by chip id. Only AF_UNIX sockets whose names match the default_socket_path()
    // convention are included; everything else in the directory is ignored. Empty if the path is
    // not a directory or holds no such sockets. This is how a client turns a socket directory into
    // the set of hosts to attach to.
    static std::map<ChipId, std::filesystem::path> sockets_in_directory(const std::filesystem::path& directory);

private:
    // Non-throwing; only initializes members. Binding happens in bind_and_listen(), driven by
    // the try_create()/create() factories.
    explicit SimulationServerSocket(const std::filesystem::path& socket_path);

    // Binds + listens (claims the path; connectable for liveness). Does not accept yet -- the
    // accept loop starts in serve(). Returns false if a live owner holds the path; throws on real
    // socket errors.
    bool bind_and_listen();

    // True if a listener is currently reachable at socket_path_.
    bool is_live();

    // Re-arms the async accept; each accepted connection is handed to a serving thread. Only ever
    // runs while serving (serve() started it), so request_handler_ is always set here.
    void do_accept();

    std::filesystem::path socket_path_;
    // Opaque request/reply handler, installed by serve(). Empty until then: with no handler the
    // accept loop never starts (serve() starts it), so the socket is presence-only -- it binds and
    // is connectable for liveness, but accepts nothing.
    RequestHandler request_handler_;
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
