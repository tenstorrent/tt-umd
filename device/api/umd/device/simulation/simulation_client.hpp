// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace tt::umd {

// Client-facing handle to a simulation host ("the card") over its per-chip UNIX socket. transact()
// moves opaque request/reply payloads; encoding lives in the simulation device that owns this
// handle, so this always-built transport carries no protocol/FlatBuffers dependency.
//
// asio is held behind a pImpl so this header stays asio-free (see Impl in the .cpp).
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

    // Sends one length-prefixed request to the host and blocks for the length-prefixed reply,
    // returning its opaque payload. Payloads are encoded/decoded by the caller (the protocol
    // layer). Throws if not attached, or if the host disconnects or errors mid-exchange.
    std::vector<uint8_t> transact(const std::vector<uint8_t>& request);

private:
    std::filesystem::path socket_path_;

    // Holds the asio transport (io_context + connected socket). Defined in the .cpp so asio
    // stays out of this header; owns the connection fd (RAII).
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::umd
