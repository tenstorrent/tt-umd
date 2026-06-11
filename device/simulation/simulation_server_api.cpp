// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_server_api.hpp"

#include <flatbuffers/flatbuffers.h>
#include <unistd.h>

#include <cerrno>

#include "simulation_server_api_generated.h"

namespace tt::umd {

namespace {

// Writes exactly n bytes, retrying short writes; false on error.
bool write_all(int fd, const uint8_t* buf, size_t n) {
    size_t offset = 0;
    while (offset < n) {
        ssize_t written = ::write(fd, buf + offset, n - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}

// Reads exactly n bytes, retrying short reads; false on EOF or error.
bool read_all(int fd, uint8_t* buf, size_t n) {
    size_t offset = 0;
    while (offset < n) {
        ssize_t got = ::read(fd, buf + offset, n - offset);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (got == 0) {
            return false;  // EOF.
        }
        offset += static_cast<size_t>(got);
    }
    return true;
}

}  // namespace

std::vector<uint8_t> encode(const Message& msg) {
    flatbuffers::FlatBufferBuilder builder;

    Body body_type = Body_NONE;
    flatbuffers::Offset<void> body;

    switch (msg.type) {
        case MessageType::AttachRequest:
            body_type = Body_AttachRequest;
            body = CreateAttachRequest(builder).Union();
            break;
        case MessageType::AttachResponse: {
            body_type = Body_AttachResponse;
            auto desc = CreateDeviceDescription(
                builder, msg.description.arch, msg.description.board, msg.description.num_chips);
            body = CreateAttachResponse(builder, desc).Union();
            break;
        }
        case MessageType::DetachRequest:
            body_type = Body_DetachRequest;
            body = CreateDetachRequest(builder).Union();
            break;
        case MessageType::DetachResponse:
            body_type = Body_DetachResponse;
            body = CreateDetachResponse(builder).Union();
            break;
        case MessageType::AdvanceExecutionRequest:
            body_type = Body_AdvanceExecutionRequest;
            body = CreateAdvanceExecutionRequest(builder).Union();
            break;
        case MessageType::AdvanceExecutionResponse:
            body_type = Body_AdvanceExecutionResponse;
            body = CreateAdvanceExecutionResponse(builder).Union();
            break;
        case MessageType::DeviceOp: {
            body_type = Body_DeviceRequestResponse;
            auto data = builder.CreateVector(msg.op.data);
            tt_vcs_core core(msg.op.endpoint.x, msg.op.endpoint.y);
            body = CreateDeviceRequestResponse(
                       builder, static_cast<DEVICE_COMMAND>(msg.op.command), data, &core, msg.op.address, msg.op.size)
                       .Union();
            break;
        }
        case MessageType::Error: {
            body_type = Body_ErrorResponse;
            auto message = builder.CreateString(msg.error_message);
            body = CreateErrorResponse(builder, msg.error_code, message).Union();
            break;
        }
    }

    builder.Finish(CreateEnvelope(builder, body_type, body));
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
}

Message decode(const uint8_t* data, size_t /*size*/) {
    Message msg;
    const Envelope* envelope = GetEnvelope(data);
    if (envelope == nullptr) {
        return msg;
    }

    switch (envelope->body_type()) {
        case Body_AttachRequest:
            msg.type = MessageType::AttachRequest;
            break;
        case Body_AttachResponse: {
            msg.type = MessageType::AttachResponse;
            const auto* response = envelope->body_as_AttachResponse();
            if (response != nullptr && response->description() != nullptr) {
                msg.description.arch = response->description()->arch();
                msg.description.board = response->description()->board();
                msg.description.num_chips = response->description()->num_chips();
            }
            break;
        }
        case Body_DetachRequest:
            msg.type = MessageType::DetachRequest;
            break;
        case Body_DetachResponse:
            msg.type = MessageType::DetachResponse;
            break;
        case Body_AdvanceExecutionRequest:
            msg.type = MessageType::AdvanceExecutionRequest;
            break;
        case Body_AdvanceExecutionResponse:
            msg.type = MessageType::AdvanceExecutionResponse;
            break;
        case Body_DeviceRequestResponse: {
            msg.type = MessageType::DeviceOp;
            const auto* op = envelope->body_as_DeviceRequestResponse();
            if (op != nullptr) {
                msg.op.command = static_cast<uint8_t>(op->command());
                if (op->core() != nullptr) {
                    msg.op.endpoint.x = static_cast<uint32_t>(op->core()->x());
                    msg.op.endpoint.y = static_cast<uint32_t>(op->core()->y());
                }
                msg.op.address = op->address();
                msg.op.size = op->size();
                if (op->data() != nullptr) {
                    msg.op.data.assign(op->data()->begin(), op->data()->end());
                }
            }
            break;
        }
        case Body_ErrorResponse: {
            msg.type = MessageType::Error;
            const auto* error = envelope->body_as_ErrorResponse();
            if (error != nullptr) {
                msg.error_code = error->code();
                if (error->message() != nullptr) {
                    msg.error_message = error->message()->str();
                }
            }
            break;
        }
        default:
            break;
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

bool send_framed(int fd, const std::vector<uint8_t>& payload) {
    const std::vector<uint8_t> framed = frame(payload);
    return write_all(fd, framed.data(), framed.size());
}

std::optional<std::vector<uint8_t>> recv_framed(int fd) {
    uint8_t length_bytes[sizeof(uint32_t)];
    if (!read_all(fd, length_bytes, sizeof(length_bytes))) {
        return std::nullopt;
    }
    const uint32_t length = length_bytes[0] | (length_bytes[1] << 8) | (length_bytes[2] << 16) |
                            (static_cast<uint32_t>(length_bytes[3]) << 24);
    std::vector<uint8_t> payload(length);
    if (length > 0 && !read_all(fd, payload.data(), length)) {
        return std::nullopt;
    }
    return payload;
}

}  // namespace tt::umd
