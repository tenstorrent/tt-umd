// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_server_api.hpp"

#include <flatbuffers/flatbuffers.h>

#include "simulation_server_api_generated.h"

namespace tt::umd {

std::vector<uint8_t> encode(const Message& msg) {
    flatbuffers::FlatBufferBuilder builder;
    auto text = builder.CreateString(msg.text);
    builder.Finish(::CreateMessage(builder, msg.value, text));
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
}

Message decode(const uint8_t* data, size_t /*size*/) {
    Message msg;
    const auto* fb = ::GetMessage(data);
    if (fb != nullptr) {
        msg.value = fb->value();
        if (fb->text() != nullptr) {
            msg.text = fb->text()->str();
        }
    }
    return msg;
}

std::vector<uint8_t> frame(const std::vector<uint8_t>& payload) {
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
