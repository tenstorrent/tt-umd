// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <asio.hpp>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

#include "simulation/simulation_server_transport.hpp"

using namespace tt::umd;
using stream_protocol = asio::local::stream_protocol;

// A connected UNIX SOCK_STREAM pair (asio's local connect_pair), mirroring the framed client/host
// connection without needing a bound socket path. Closing one end simulates a peer going away.
class SimulationServerTransportTest : public ::testing::Test {
protected:
    void SetUp() override { asio::local::connect_pair(a_, b_); }

    asio::io_context io_;
    stream_protocol::socket a_{io_};
    stream_protocol::socket b_{io_};
};

// A message sent on one end comes back whole on the other.
TEST_F(SimulationServerTransportTest, SingleMessageRoundTrip) {
    const std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};

    send_framed(a_, payload);
    EXPECT_EQ(recv_framed(b_), payload);
}

// Framing keeps message boundaries on a stream: two messages sent back-to-back are read back as
// two distinct messages, not one merged blob.
TEST_F(SimulationServerTransportTest, BackToBackMessagesPreserveBoundaries) {
    const std::vector<uint8_t> first = {0xAA};
    const std::vector<uint8_t> second = {0xBB, 0xCC, 0xDD};

    send_framed(a_, first);
    send_framed(a_, second);

    EXPECT_EQ(recv_framed(b_), first);
    EXPECT_EQ(recv_framed(b_), second);
}

// A zero-length payload is a valid frame (just the length prefix).
TEST_F(SimulationServerTransportTest, EmptyPayloadRoundTrip) {
    send_framed(a_, {});
    EXPECT_TRUE(recv_framed(b_).empty());
}

// A payload larger than the socket buffer arrives across multiple reads. The send runs on a thread
// because such a write can block until the reader drains the buffer.
TEST_F(SimulationServerTransportTest, LargePayloadSpanningMultipleReads) {
    std::vector<uint8_t> payload(1u << 20);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }

    std::thread sender([&] { send_framed(a_, payload); });
    const std::vector<uint8_t> received = recv_framed(b_);
    sender.join();

    EXPECT_EQ(received, payload);
}

// If the peer closes at a message boundary (no message coming), recv surfaces an error rather than
// blocking forever or returning a bogus empty message.
TEST_F(SimulationServerTransportTest, RecvThrowsWhenPeerCloses) {
    a_.close();

    EXPECT_THROW(recv_framed(b_), std::exception);
}

// A message cut short (header promises more than is delivered, then the peer goes away) fails
// instead of returning a partial payload.
TEST_F(SimulationServerTransportTest, RecvThrowsOnTruncatedMessage) {
    const uint8_t header[4] = {10, 0, 0, 0};  // claims a 10-byte payload
    asio::write(a_, asio::buffer(header, sizeof(header)));
    const uint8_t partial[3] = {1, 2, 3};  // but only 3 bytes follow
    asio::write(a_, asio::buffer(partial, sizeof(partial)));
    a_.close();

    EXPECT_THROW(recv_framed(b_), std::exception);
}

// A header advertising an absurdly large payload is rejected at the length check, before recv
// tries to allocate it -- an untrusted/garbled peer can't trigger a multi-GB allocation.
TEST_F(SimulationServerTransportTest, RecvThrowsOnOversizedFrame) {
    const uint8_t header[4] = {0xFF, 0xFF, 0xFF, 0xFF};  // ~4 GiB length prefix
    asio::write(a_, asio::buffer(header, sizeof(header)));

    EXPECT_THROW(recv_framed(b_), std::exception);
}

// Sending to a peer that has closed surfaces as a thrown error, not a SIGPIPE that kills the
// process (asio sets MSG_NOSIGNAL on the write).
TEST_F(SimulationServerTransportTest, SendToClosedPeerThrowsRatherThanRaisingSigpipe) {
    b_.close();

    EXPECT_THROW(send_framed(a_, {0x01, 0x02, 0x03}), std::exception);
}
