// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <vector>

#include "umd/device/simulation/simulation_server_protocol.hpp"

using namespace tt::umd;

// A read request carries the access geometry and no payload; encode -> decode reproduces it.
TEST(SimulationServerProtocol, ReadRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Read;
    request.x = 3;
    request.y = 5;
    request.address = 0x1000'2000'3000'4000ULL;
    request.size = 256;

    const SimulationServerRequest decoded = decode_request(encode(request));

    EXPECT_EQ(decoded.command, SimulationServerCommand::Read);
    EXPECT_EQ(decoded.x, request.x);
    EXPECT_EQ(decoded.y, request.y);
    EXPECT_EQ(decoded.address, request.address);
    EXPECT_EQ(decoded.size, request.size);
    EXPECT_TRUE(decoded.data.empty());
}

// A write request carries its payload; encode -> decode reproduces geometry and bytes.
TEST(SimulationServerProtocol, WriteRequestRoundTrip) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Write;
    request.x = 1;
    request.y = 2;
    request.address = 0xDEAD'BEEFULL;
    request.data = {0x11, 0x22, 0x33, 0x44, 0x55};
    request.size = static_cast<uint32_t>(request.data.size());

    const SimulationServerRequest decoded = decode_request(encode(request));

    EXPECT_EQ(decoded.command, SimulationServerCommand::Write);
    EXPECT_EQ(decoded.x, request.x);
    EXPECT_EQ(decoded.y, request.y);
    EXPECT_EQ(decoded.address, request.address);
    EXPECT_EQ(decoded.size, request.size);
    EXPECT_EQ(decoded.data, request.data);
}

// A read response carries the bytes read and a success status.
TEST(SimulationServerProtocol, ReadResponseRoundTrip) {
    SimulationServerResponse response;
    response.status = 0;
    response.data = {0xAA, 0xBB, 0xCC};

    const SimulationServerResponse decoded = decode_response(encode(response));

    EXPECT_EQ(decoded.status, 0);
    EXPECT_EQ(decoded.data, response.data);
}

// A failed response carries a nonzero status and no payload.
TEST(SimulationServerProtocol, ErrorResponseRoundTrip) {
    SimulationServerResponse response;
    response.status = -5;

    const SimulationServerResponse decoded = decode_response(encode(response));

    EXPECT_EQ(decoded.status, -5);
    EXPECT_TRUE(decoded.data.empty());
}

// An empty buffer can never be a valid message; decoding it fails rather than reading nothing.
TEST(SimulationServerProtocol, DecodeEmptyBufferThrows) {
    const std::vector<uint8_t> empty;
    EXPECT_THROW(decode_request(empty), std::exception);
    EXPECT_THROW(decode_response(empty), std::exception);
}

// A truncated payload (a valid message cut short, as a partial socket read would produce) fails
// verification instead of reading out of bounds.
TEST(SimulationServerProtocol, DecodeTruncatedBufferThrows) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::Read;
    request.size = 64;
    std::vector<uint8_t> encoded = encode(request);
    ASSERT_GT(encoded.size(), 4u);
    encoded.resize(encoded.size() / 2);  // chop it in half

    EXPECT_THROW(decode_request(encoded), std::exception);
}
