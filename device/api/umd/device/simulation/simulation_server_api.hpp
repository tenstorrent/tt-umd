// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

// The single place that defines the UMD <-> simulation host API: the operations a UMD client
// calls against a simulation host ("the card"). For now it carries just the session handshake; it
// grows to mirror the device operations UMD performs against silicon as the server work lands, so
// the client stays a thin pass-through. See docs/SIMULATION_SERVER_PROCESS.md (§8).
//
// This is intentionally just the contract. Implementations come in later issues: the host-side
// dispatch (#2794) and the socket-backed client (#2795). Extend this interface here as new
// operations are needed — it is the one definition point clients call into.
class SimulationServerApi {
public:
    virtual ~SimulationServerApi() = default;

    // Session. attach registers the client (non-mutating); detach releases it.
    virtual void attach() = 0;
    virtual void detach() = 0;

    // TODO: define the rest of the surface here as the server work lands (§8):
    //   - device description / identity (topology + memory layout)
    //   - device memory access (read / write / multicast)
    //   - TLB windows (allocate / configure / free)
    //   - host memory / sysmem (allocate / free / access)
    //   - run / reset control
    //   - execution / time
    //   - management (shutdown)
};

// ---------------------------------------------------------------------------
// Wire layer: the FlatBuffers serialization path every API operation goes through. Each API
// operation is one message, tagged by MessageType; memory/reset/run ops reuse the existing
// simulation_device.fbs DeviceRequestResponse. FlatBuffers stays an implementation detail (it
// lives in the .cpp).
// ---------------------------------------------------------------------------

enum class MessageType : uint8_t {
    AttachRequest,
    AttachResponse,
    DetachRequest,
    DetachResponse,
    AdvanceExecutionRequest,
    AdvanceExecutionResponse,
    DeviceOp,  // memory / reset / run op (reuses simulation_device.fbs DeviceRequestResponse)
    Error,
};

struct Endpoint {
    uint32_t x = 0;
    uint32_t y = 0;
};

// Mirrors the reused DeviceRequestResponse: a memory/reset/run operation and its response.
struct DeviceOp {
    uint8_t command = 0;  // DEVICE_COMMAND value from simulation_device.fbs
    Endpoint endpoint;
    uint64_t address = 0;
    uint32_t size = 0;
    std::vector<uint32_t> data;
};

// Identity and topology a client reads from the host on attach, carried as the AttachResponse.
// Placeholder — extended as the server work lands (full SoC descriptor / cluster topology /
// memory layout).
struct SimulationDeviceDescription {
    uint32_t arch = 0;
    uint32_t board = 0;
    uint32_t num_chips = 0;
};

// A single API message. The active fields are determined by `type`.
struct Message {
    MessageType type = MessageType::Error;
    SimulationDeviceDescription description;  // AttachResponse
    DeviceOp op;                              // DeviceOp
    uint32_t error_code = 0;                  // Error
    std::string error_message;                // Error
};

// Serialize a message to a FlatBuffers payload, and parse one back.
std::vector<uint8_t> encode(const Message& msg);
Message decode(const uint8_t* data, size_t size);

inline Message decode(const std::vector<uint8_t>& bytes) {
    // An empty buffer can never be a valid message; forwarding bytes.data() (which may be null)
    // would push the empty/invalid case down to the verifier, so reject it here.
    if (bytes.empty()) {
        UMD_THROW(error::RuntimeError, "Cannot decode an empty simulation server API buffer");
    }
    return decode(bytes.data(), bytes.size());
}

// Length-prefix framing for the stream socket: a 4-byte little-endian length, then the payload.
std::vector<uint8_t> frame(const std::vector<uint8_t>& payload);

// Blocking length-prefixed transport over a connected stream socket fd. send_framed writes a
// framed payload; recv_framed reads one, returning nullopt on EOF or error.
bool send_framed(int fd, const std::vector<uint8_t>& payload);
std::optional<std::vector<uint8_t>> recv_framed(int fd);

}  // namespace tt::umd
