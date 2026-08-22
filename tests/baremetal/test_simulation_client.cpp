// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "umd/device/simulation/simulation_client.hpp"

using namespace tt::umd;

namespace {

class SimulationClientTest : public ::testing::Test {
protected:
    // Clear any stale socket file (e.g. from a previous crashed run on PID reuse) so bind() and
    // connect() start from a clean path, mirroring the SimulationServerSocket tests. The PID in the
    // name keeps parallel test binaries from colliding.
    void SetUp() override { std::filesystem::remove(path_); }

    void TearDown() override { std::filesystem::remove(path_); }

    std::filesystem::path path_ =
        std::filesystem::temp_directory_path() / ("tt-umd-sim-client-test-" + std::to_string(::getpid()) + ".sock");
};

}  // namespace

// attach() connects to a live host's socket; detach() closes and is idempotent.
TEST_F(SimulationClientTest, AttachesToLiveHostThenDetaches) {
    auto server = SimulationServerSocket::create(path_);  // Binds + listens => a live host at path_.

    SimulationClient client(path_);
    EXPECT_NO_THROW(client.attach());
    EXPECT_NO_THROW(client.detach());
    EXPECT_NO_THROW(client.detach());  // Idempotent.
}

// With no host bound at the path, attach() fails loudly rather than silently producing a
// half-connected client.
TEST_F(SimulationClientTest, AttachThrowsWhenNoHost) {
    SimulationClient client(path_);
    EXPECT_THROW(client.attach(), std::exception);
}

// The destructor releases the connection even if detach() was never called.
TEST_F(SimulationClientTest, DestructorDetaches) {
    auto server = SimulationServerSocket::create(path_);

    {
        SimulationClient client(path_);
        EXPECT_NO_THROW(client.attach());
    }
}

// End to end over the socket: transact() returns the host's reply, not the request it sent (the
// handler answers with a fixed payload, so the reply provably comes from the host).
TEST_F(SimulationClientTest, TransactReturnsHostReply) {
    auto server = SimulationServerSocket::create(path_);
    server->serve([](const std::vector<uint8_t>&) { return std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}; });
    SimulationClient client(path_);
    client.attach();

    EXPECT_EQ(client.transact({0x01, 0x02, 0x03}), (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}

// One attached client carries many request/reply turns (here the host echoes each request back).
TEST_F(SimulationClientTest, TransactHandlesSequentialRequests) {
    auto server = SimulationServerSocket::create(path_);
    server->serve([](const std::vector<uint8_t>& request) { return request; });
    SimulationClient client(path_);
    client.attach();

    for (uint8_t i = 0; i < 5; ++i) {
        const std::vector<uint8_t> request = {i, static_cast<uint8_t>(i + 1)};
        EXPECT_EQ(client.transact(request), request);
    }
}

// transact() before attach() fails loudly rather than touching a closed socket.
TEST_F(SimulationClientTest, TransactThrowsWhenNotAttached) {
    SimulationClient client(path_);
    EXPECT_THROW(client.transact({0x01}), std::exception);
}

// If the host goes away while attached, transact() surfaces an error (and does not raise SIGPIPE
// writing to the dead peer) rather than hanging or crashing.
TEST_F(SimulationClientTest, TransactThrowsAfterHostGone) {
    SimulationClient client(path_);
    {
        auto server = SimulationServerSocket::create(path_);
        server->serve([](const std::vector<uint8_t>& request) { return request; });
        client.attach();
        EXPECT_EQ(client.transact({0x7F}), (std::vector<uint8_t>{0x7F}));  // works while the host is up
    }                                                                      // host destroyed here

    EXPECT_THROW(client.transact({0x01}), std::exception);
}
