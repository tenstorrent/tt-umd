// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "umd/device/simulation/simulation_server_api.hpp"

using namespace tt::umd;

namespace {

// A trivial implementation, demonstrating that a client can be written against the API contract.
// Stands in for the real socket-backed client (#2795) until it lands.
class FakeSimulationServerClient : public SimulationServerApi {
public:
    void attach() override { attached_ = true; }

    void detach() override { attached_ = false; }

    bool attached() const { return attached_; }

private:
    bool attached_ = false;
};

}  // namespace

TEST(SimulationServerApi, ClientCanImplementAndCallTheContract) {
    FakeSimulationServerClient client;

    client.attach();
    EXPECT_TRUE(client.attached());

    client.detach();
    EXPECT_FALSE(client.attached());
}

// The dummy message proves the FlatBuffers serialization path works end-to-end.
TEST(SimulationServerApiWire, MessageRoundTrip) {
    Message msg;
    msg.payload = {0xde, 0xad, 0xbe, 0xef};

    const Message out = decode(encode(msg));

    EXPECT_EQ(out.payload, msg.payload);
}

TEST(SimulationServerApiWire, FramePrependsLittleEndianLength) {
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};

    const std::vector<uint8_t> framed = frame(payload);

    ASSERT_EQ(framed.size(), payload.size() + 4);
    const uint32_t len = framed[0] | (framed[1] << 8) | (framed[2] << 16) | (uint32_t(framed[3]) << 24);
    EXPECT_EQ(len, payload.size());
    EXPECT_EQ(std::vector<uint8_t>(framed.begin() + 4, framed.end()), payload);
}
