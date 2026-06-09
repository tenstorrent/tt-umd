// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_server_api.hpp"

#include <flatbuffers/flatbuffers.h>
#include <fmt/format.h>

#include <limits>

#include "simulation_server_api_generated.h"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::vector<uint8_t> encode(const Message& msg) {
    flatbuffers::FlatBufferBuilder builder;
    auto payload = builder.CreateVector(msg.payload);
    builder.Finish(::CreateMessage(builder, payload));
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
}

Message decode(const uint8_t* data, size_t size) {
    // This parses untrusted stream/socket input, so verify the buffer against the schema (using
    // the provided size) before touching any field — fail fast rather than risk out-of-bounds
    // reads on a malformed or truncated payload.
    flatbuffers::Verifier verifier(data, size);
    if (data == nullptr || !::VerifyMessageBuffer(verifier)) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to verify simulation server API message ({} bytes)", size));
    }

    Message msg;
    const auto* fb = ::GetMessage(data);
    if (fb->payload() != nullptr) {
        msg.payload.assign(fb->payload()->begin(), fb->payload()->end());
    }
    return msg;
}

std::vector<uint8_t> frame(const std::vector<uint8_t>& payload) {
    // The length prefix is a uint32_t; guard against an oversized payload silently wrapping it
    // (which would desynchronize the receiver) rather than truncating.
    UMD_ASSERT(
        payload.size() <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format("Simulation server API payload too large to frame ({} bytes)", payload.size()));
    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> framed;
    framed.reserve(payload.size() + sizeof(uint32_t));
    framed.push_back(length & 0xFF);
    framed.push_back((length >> 8) & 0xFF);
    framed.push_back((length >> 16) & 0xFF);
    framed.push_back((length >> 24) & 0xFF);
    framed.insert(framed.end(), payload.begin(), payload.end());
    return framed;
}

}  // namespace tt::umd
