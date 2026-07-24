// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_server_protocol.hpp"

#include <flatbuffers/flatbuffers.h>
#include <fmt/format.h>

#include "simulation_server_protocol_generated.h"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// Verifies a FlatBuffers payload against schema table T (the buffer is untrusted socket input, so
// this must run before any field access) and returns the parsed root. Throws on an empty, null,
// or malformed/truncated buffer.
template <typename T>
const T* verify_and_get_root(const uint8_t* data, size_t size, const char* what) {
    flatbuffers::Verifier verifier(data, size);
    if (data == nullptr || !verifier.VerifyBuffer<T>(nullptr)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to verify simulation server protocol {} message ({} bytes)", what, size));
    }
    return flatbuffers::GetRoot<T>(data);
}

std::vector<uint8_t> to_bytes(const flatbuffers::FlatBufferBuilder& builder) {
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
}

}  // namespace

std::vector<uint8_t> encode(const SimulationServerRequest& request) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(request.data);
    builder.Finish(wire::CreateSimulationServerRequest(
        builder,
        static_cast<wire::SimulationServerCommand>(request.command),
        request.x,
        request.y,
        request.address,
        request.size,
        data));
    return to_bytes(builder);
}

std::vector<uint8_t> encode(const SimulationServerResponse& response) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(response.data);
    builder.Finish(wire::CreateSimulationServerResponse(builder, response.status, data));
    return to_bytes(builder);
}

SimulationServerRequest decode_request(const uint8_t* data, size_t size) {
    const auto* fb = verify_and_get_root<wire::SimulationServerRequest>(data, size, "request");
    SimulationServerRequest request;
    request.command = static_cast<SimulationServerCommand>(fb->command());
    request.x = fb->x();
    request.y = fb->y();
    request.address = fb->address();
    request.size = fb->size();
    if (fb->data() != nullptr) {
        request.data.assign(fb->data()->begin(), fb->data()->end());
    }
    return request;
}

SimulationServerResponse decode_response(const uint8_t* data, size_t size) {
    const auto* fb = verify_and_get_root<wire::SimulationServerResponse>(data, size, "response");
    SimulationServerResponse response;
    response.status = fb->status();
    if (fb->data() != nullptr) {
        response.data.assign(fb->data()->begin(), fb->data()->end());
    }
    return response;
}

SimulationServerRequest decode_request(const std::vector<uint8_t>& bytes) {
    return decode_request(bytes.data(), bytes.size());
}

SimulationServerResponse decode_response(const std::vector<uint8_t>& bytes) {
    return decode_response(bytes.data(), bytes.size());
}

}  // namespace tt::umd
