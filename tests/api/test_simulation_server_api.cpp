// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

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

namespace {
constexpr uint8_t CMD_WRITE = 0;  // DEVICE_COMMAND values from simulation_device.fbs.
constexpr uint8_t CMD_READ = 1;
}  // namespace

TEST(SimulationServerApiWire, DeviceOpReadRequestRoundTrip) {
    Message msg;
    msg.type = MessageType::DeviceOp;
    msg.op.command = CMD_READ;
    msg.op.endpoint = {2, 3};
    msg.op.address = 0x1000;
    msg.op.size = 64;

    const Message out = decode(encode(msg));

    EXPECT_EQ(out.type, MessageType::DeviceOp);
    EXPECT_EQ(out.op.command, CMD_READ);
    EXPECT_EQ(out.op.endpoint.x, 2u);
    EXPECT_EQ(out.op.endpoint.y, 3u);
    EXPECT_EQ(out.op.address, 0x1000u);
    EXPECT_EQ(out.op.size, 64u);
}

TEST(SimulationServerApiWire, DeviceOpWriteCarriesData) {
    Message msg;
    msg.type = MessageType::DeviceOp;
    msg.op.command = CMD_WRITE;
    msg.op.endpoint = {1, 1};
    msg.op.address = 0x2000;
    msg.op.data = {0xdeadbeef, 0x12345678, 0x0};

    const Message out = decode(encode(msg));

    EXPECT_EQ(out.type, MessageType::DeviceOp);
    EXPECT_EQ(out.op.command, CMD_WRITE);
    EXPECT_EQ(out.op.data, std::vector<uint32_t>({0xdeadbeef, 0x12345678, 0x0}));
}

TEST(SimulationServerApiWire, AttachResponseCarriesDeviceDescription) {
    Message msg;
    msg.type = MessageType::AttachResponse;
    msg.description = {/*arch=*/5, /*board=*/2, /*num_chips=*/4};

    const Message out = decode(encode(msg));

    EXPECT_EQ(out.type, MessageType::AttachResponse);
    EXPECT_EQ(out.description.arch, 5u);
    EXPECT_EQ(out.description.board, 2u);
    EXPECT_EQ(out.description.num_chips, 4u);
}

TEST(SimulationServerApiWire, ErrorRoundTrip) {
    Message msg;
    msg.type = MessageType::Error;
    msg.error_code = 7;
    msg.error_message = "boom";

    const Message out = decode(encode(msg));

    EXPECT_EQ(out.type, MessageType::Error);
    EXPECT_EQ(out.error_code, 7u);
    EXPECT_EQ(out.error_message, "boom");
}

TEST(SimulationServerApiWire, AdvanceExecutionRoundTrip) {
    Message request;
    request.type = MessageType::AdvanceExecutionRequest;
    EXPECT_EQ(decode(encode(request)).type, MessageType::AdvanceExecutionRequest);

    Message response;
    response.type = MessageType::AdvanceExecutionResponse;
    EXPECT_EQ(decode(encode(response)).type, MessageType::AdvanceExecutionResponse);
}

TEST(SimulationServerApiWire, FramePrependsLittleEndianLength) {
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};

    const std::vector<uint8_t> framed = frame(payload);

    ASSERT_EQ(framed.size(), payload.size() + 4);
    const uint32_t len = framed[0] | (framed[1] << 8) | (framed[2] << 16) | (uint32_t(framed[3]) << 24);
    EXPECT_EQ(len, payload.size());
    EXPECT_EQ(std::vector<uint8_t>(framed.begin() + 4, framed.end()), payload);
}

TEST(SimulationServerApiWire, FramedSendRecvOverSocketpair) {
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    const std::vector<uint8_t> payload = {9, 8, 7, 6};
    ASSERT_TRUE(send_framed(fds[0], payload));
    const auto received = recv_framed(fds[1]);

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, payload);

    ::close(fds[0]);
    ::close(fds[1]);
}
