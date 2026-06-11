// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Owns the per-chip UNIX domain socket that exposes one simulation device on disk
// ("the card"). One socket per simulated device.
//
// Binding the socket is the act of surfacing the device; a client proves liveness
// by connecting to it. On construction it reclaims a stale socket left by a dead
// owner. Whether a live owner already holds the path is the host-vs-client arbiter:
// the throwing constructor refuses in that case, while try_create() returns nullptr so
// the caller (SimulationTopologyDiscovery) can attach as a client instead.
//
// Serving is deferred: until start_serving() is called, connections are accepted and
// dropped (liveness only). With a handler, each connection gets its own worker thread
// that reads a framed request, invokes the handler, and writes the framed response,
// until the client disconnects. On destruction it stops serving and removes the file.
//
// Note: distinct from SimulationHost, which is the (nng) RTL transport over which the
// simulator process connects back into UMD.
class SimulationSocket {
public:
    // Handles one request payload and returns the response payload. Invoked on a per-connection
    // worker thread; the implementation is responsible for any cross-client serialization.
    using RequestHandler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

    // Binds and listens; throws if a live owner already holds the path.
    explicit SimulationSocket(const std::filesystem::path& socket_path);
    ~SimulationSocket();

    SimulationSocket(const SimulationSocket&) = delete;
    SimulationSocket& operator=(const SimulationSocket&) = delete;

    // Binds and listens, reclaiming a stale socket. Returns nullptr if a *live* owner already
    // holds the path (so the caller can attach as a client). Throws on real socket errors.
    static std::unique_ptr<SimulationSocket> try_create(const std::filesystem::path& socket_path);

    // Starts serving requests through the handler. Until called, connections are accepted and
    // dropped. Called once, by the host, after the device it dispatches into exists.
    void start_serving(RequestHandler handler);

    const std::filesystem::path& socket_path() const { return socket_path_; }

    // Deterministic per-chip socket path (the naming contract a future client connects to):
    // <dir>/tt-umd-sim-<uid>-<chip_id>.sock, where <dir> is $TT_UMD_SIM_SOCKET_DIR if set,
    // otherwise the system temp directory.
    static std::filesystem::path default_socket_path(ChipId chip_id = 0);

private:
    struct DeferBind {};

    SimulationSocket(const std::filesystem::path& socket_path, DeferBind);

    // Binds + listens + starts the accept loop. Returns false if a live owner holds the path;
    // throws on real socket errors.
    bool bind_and_listen();

    void accept_loop();
    void serve_connection(int client_fd, size_t index);

    std::filesystem::path socket_path_;
    int listen_fd_ = -1;
    int shutdown_pipe_[2] = {-1, -1};
    std::thread accept_thread_;
    std::atomic<bool> running_{false};
    // True only once this instance has bound the path, so the destructor never removes a
    // socket file owned by a *different* (live) host (e.g. when try_create returns nullptr).
    bool owns_socket_file_ = false;

    // Set once via start_serving(); read under connections_mutex_. Null = accept-and-drop.
    std::mutex connections_mutex_;
    RequestHandler handler_;
    // Per-connection worker threads and their fds. Threads are joined at destruction; a worker
    // closes its own fd (under the mutex) on exit. TODO: reap finished workers during the run.
    std::vector<std::thread> connection_threads_;
    std::vector<int> connection_fds_;
};

}  // namespace tt::umd
