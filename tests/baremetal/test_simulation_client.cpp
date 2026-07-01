// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <string>

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
