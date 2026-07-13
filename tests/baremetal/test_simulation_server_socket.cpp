// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "simulation/simulation_server_socket.hpp"

using namespace tt::umd;

namespace {

// A socket path under the temp dir, named with the pid so concurrent test binaries don't
// collide. A fixed name is enough because the fixture removes it around every test.
std::filesystem::path test_socket_path() {
    return std::filesystem::temp_directory_path() / ("tt-umd-sim-server-test-" + std::to_string(::getpid()) + ".sock");
}

// Attempts a connect() to a UNIX socket at path. Returns true if it connected.
bool can_connect(const std::filesystem::path& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

// Binds a UNIX socket to path then closes it without listening, leaving a stale
// socket file behind (connect() to it yields ECONNREFUSED) — what a crashed
// owner leaves on disk.
void leave_stale_socket(const std::filesystem::path& path) {
    std::filesystem::remove(path);
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ::close(fd);
    ASSERT_TRUE(std::filesystem::exists(path));
}

// Provides a fresh socket path that is removed before and after each test, so cases share a
// fixed name without leaking sockets between runs.
class SimulationServerSocketTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = test_socket_path();
        std::filesystem::remove(path_);
    }

    void TearDown() override { std::filesystem::remove(path_); }

    std::filesystem::path path_;
};

}  // namespace

TEST_F(SimulationServerSocketTest, ExposesConnectableSocket) {
    auto server = SimulationServerSocket::create(path_);

    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_TRUE(can_connect(path_));
}

TEST_F(SimulationServerSocketTest, RemovesSocketOnDestruction) {
    { auto server = SimulationServerSocket::create(path_); }

    EXPECT_FALSE(std::filesystem::exists(path_));
    EXPECT_FALSE(can_connect(path_));
}

TEST_F(SimulationServerSocketTest, ReclaimsStaleSocketFile) {
    leave_stale_socket(path_);
    EXPECT_FALSE(can_connect(path_));

    auto server = SimulationServerSocket::create(path_);

    EXPECT_TRUE(can_connect(path_));
}

TEST_F(SimulationServerSocketTest, ThrowsWhenLiveServerAlreadyExists) {
    auto server = SimulationServerSocket::create(path_);

    EXPECT_ANY_THROW(SimulationServerSocket::create(path_));
}

TEST_F(SimulationServerSocketTest, TryCreateReturnsNullWhenLiveHostExists) {
    auto host = SimulationServerSocket::try_create(path_);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(SimulationServerSocket::try_create(path_), nullptr);  // live host -> null, no throw

    // The throwaway object from the failed try_create above must not remove the live
    // host's socket on destruction (ownership-gated teardown).
    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_TRUE(can_connect(path_));
}

TEST_F(SimulationServerSocketTest, TryCreateReclaimsStaleSocket) {
    leave_stale_socket(path_);

    auto host = SimulationServerSocket::try_create(path_);
    EXPECT_NE(host, nullptr);  // stale leftover reclaimed
    EXPECT_TRUE(can_connect(path_));
}

// A non-socket file squatting the path also yields EADDRINUSE on bind; the reclaim path
// must refuse to delete it instead of destroying unrelated data.
TEST_F(SimulationServerSocketTest, RefusesToReclaimNonSocketFile) {
    { std::ofstream(path_) << "not a socket"; }

    EXPECT_THROW(SimulationServerSocket::try_create(path_), std::exception);
    EXPECT_TRUE(std::filesystem::exists(path_));  // the regular file was left untouched
}

TEST(SimulationServerSocket, DefaultSocketPathIsPerChip) {
    EXPECT_NE(SimulationServerSocket::default_socket_path(0), SimulationServerSocket::default_socket_path(1));
}

TEST(SimulationServerSocket, DefaultSocketPathIsAbsolute) {
    EXPECT_TRUE(SimulationServerSocket::default_socket_path(0).is_absolute());
}
