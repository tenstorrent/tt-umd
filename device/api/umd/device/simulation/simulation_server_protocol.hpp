// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tt::umd {

// The one place that defines the UMD <-> simulation host wire protocol: the messages a UMD
// client exchanges with a simulation host ("the card") over its per-chip UNIX socket, plus the
// helpers that (de)serialize them. See simulation_server_protocol.fbs for the schema.
//
// Scope today is device-memory access -- the bulk of the traffic. It grows one message at a time
// as the server work lands (session/attach, TLB windows, sysmem, run/reset, ...), at which point
// the socket-backed client and the host serving layer marshal their operations through here.
//
// FlatBuffers stays an implementation detail (it lives in the .cpp); callers see only these plain
// structs and free functions. Stream framing (length-prefixing messages on the socket) is a
// transport concern and lives with the socket send/recv helpers, not here.

// Direction of a device-memory access; mirrors wire::SimulationServerCommand in the schema.
enum class SimulationServerCommand : int8_t {
    Read = 0,
    Write = 1,
};

// A device-memory access request addressed by (core x/y, address, size).
struct SimulationServerRequest {
    SimulationServerCommand command = SimulationServerCommand::Read;
    uint32_t x = 0;
    uint32_t y = 0;
    uint64_t address = 0;
    uint32_t size = 0;
    // Bytes to write (length == size) for Write; empty for Read.
    std::vector<uint8_t> data;
};

// The response to a SimulationServerRequest.
struct SimulationServerResponse {
    // 0 on success; nonzero signals a host-side failure.
    int32_t status = 0;
    // Bytes read (length == size on success) for Read; empty for Write.
    std::vector<uint8_t> data;
};

// Serialize a message to a FlatBuffers payload.
std::vector<uint8_t> encode(const SimulationServerRequest& request);
std::vector<uint8_t> encode(const SimulationServerResponse& response);

// Parse a message back from a FlatBuffers payload. The buffer is verified against the schema
// before any field is read (it comes off a socket, so it may be malformed or truncated); a bad
// or empty buffer throws error::RuntimeError rather than risking an out-of-bounds read.
SimulationServerRequest decode_request(const uint8_t* data, size_t size);
SimulationServerResponse decode_response(const uint8_t* data, size_t size);
SimulationServerRequest decode_request(const std::vector<uint8_t>& bytes);
SimulationServerResponse decode_response(const std::vector<uint8_t>& bytes);

}  // namespace tt::umd
