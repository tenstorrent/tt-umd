// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

// Identity and topology a client reads from the host on attach. Placeholder — extended as the
// server work lands (full SoC descriptor / cluster topology / memory layout).
struct SimulationDeviceDescription {
    uint32_t arch = 0;
    uint32_t board = 0;
    uint32_t num_chips = 0;
};

// The single place that defines the UMD <-> simulation host API: the operations a UMD client
// calls against a simulation host ("the card"). It mirrors the device operations UMD already
// performs against silicon, so the client stays a thin pass-through, plus the session verbs that
// multi-client sharing needs. See docs/SIMULATION_SERVER_PROCESS.md (§8).
//
// This is intentionally just the contract. Implementations come in later issues: the host-side
// dispatch (#2794) and the socket-backed client (#2795). Extend this interface here as new
// operations are needed — it is the one definition point clients call into.
class SimulationServerApi {
public:
    virtual ~SimulationServerApi() = default;

    // Session. Attach is non-mutating: it registers the client and returns the device
    // description; detach releases it.
    virtual SimulationDeviceDescription attach() = 0;
    virtual void detach() = 0;

    // Device memory access, addressed by (endpoint, address, size).
    virtual void read_from_device(CoreCoord endpoint, uint64_t address, void* dst, size_t size) = 0;
    virtual void write_to_device(CoreCoord endpoint, uint64_t address, const void* src, size_t size) = 0;

    // TODO: define the rest of the surface here as the server work lands (§8):
    //   - TLB windows (allocate / configure / free)
    //   - host memory / sysmem (allocate / free / access)
    //   - run / reset control
    //   - execution / time
    //   - management (shutdown)
};

// ---------------------------------------------------------------------------
// Wire layer: the FlatBuffers serialization path every API operation will go through.
//
// Placeholder for now: a single message that proves the serialization + framing path works
// end-to-end. The real per-operation messages replace it when the protocol is wired into the
// serving layer (#2794) and the socket-backed client (#2795). FlatBuffers stays an implementation
// detail (it lives in the .cpp).
// ---------------------------------------------------------------------------

struct Message {
    uint32_t value = 0;
    std::string text;
};

// Serialize a message to a FlatBuffers payload, and parse one back.
std::vector<uint8_t> encode(const Message& msg);
Message decode(const uint8_t* data, size_t size);

inline Message decode(const std::vector<uint8_t>& bytes) { return decode(bytes.data(), bytes.size()); }

// Length-prefix framing for the stream socket: a 4-byte little-endian length, then the payload.
std::vector<uint8_t> frame(const std::vector<uint8_t>& payload);

}  // namespace tt::umd
