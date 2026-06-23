// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>

namespace tt::umd {

// The client-facing handle to a simulation host ("the card"), reached over its per-chip UNIX
// socket. Deliberately slim for now: just the session handshake -- attach() connects, detach()
// closes. It grows operation-by-operation as the server work lands (device description, memory
// access, TLB, sysmem, run/reset, ...), at which point TTSimTTDevice's client-mode dispatch is
// rebound from its throwing stubs onto these calls. No simulation build needed (asio is a core
// dependency), mirroring SimulationSocket on the host side.
//
// The asio transport is held behind a pImpl so this header stays free of asio (a private
// dependency of the library) -- see Impl in the .cpp.
class SimulationClient {
public:
    explicit SimulationClient(std::filesystem::path socket_path);
    ~SimulationClient();

    SimulationClient(const SimulationClient&) = delete;
    SimulationClient& operator=(const SimulationClient&) = delete;

    // Connects to the host socket; throws if no live host is reachable at the path. Idempotent
    // once connected.
    void attach();
    // Closes the connection; idempotent.
    void detach();

private:
    std::filesystem::path socket_path_;

    // Holds the asio transport (io_context + connected socket). Defined in the .cpp so asio
    // stays out of this header; owns the connection fd (RAII).
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::umd
