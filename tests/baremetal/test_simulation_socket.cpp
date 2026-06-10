// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>

#include "umd/device/simulation/simulation_server_api.hpp"
#include "umd/device/simulation/simulation_socket.hpp"

using namespace tt::umd;

namespace {

// A unique-per-test socket path under the temp dir, named with the pid and the
// caller's tag so concurrent test binaries / cases don't collide.
std::filesystem::path unique_socket_path(int tag) {
    return std::filesystem::temp_directory_path() /
           ("tt-umd-sim-server-test-" + std::to_string(::getpid()) + "-" + std::to_string(tag) + ".sock");
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

// Connects to a UNIX socket at path, returning the connected fd or -1.
int connect_fd(const std::filesystem::path& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
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

}  // namespace

TEST(SimulationSocket, ExposesConnectableSocket) {
    const std::filesystem::path path = unique_socket_path(1);
    std::filesystem::remove(path);

    SimulationSocket server(path);

    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(can_connect(path));

    std::filesystem::remove(path);
}

TEST(SimulationSocket, RemovesSocketOnDestruction) {
    const std::filesystem::path path = unique_socket_path(2);
    std::filesystem::remove(path);

    { SimulationSocket server(path); }

    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(can_connect(path));
}

TEST(SimulationSocket, ReclaimsStaleSocketFile) {
    const std::filesystem::path path = unique_socket_path(3);
    leave_stale_socket(path);
    EXPECT_FALSE(can_connect(path));

    SimulationSocket server(path);

    EXPECT_TRUE(can_connect(path));

    std::filesystem::remove(path);
}

TEST(SimulationSocket, ThrowsWhenLiveServerAlreadyExists) {
    const std::filesystem::path path = unique_socket_path(4);
    std::filesystem::remove(path);

    SimulationSocket server(path);

    EXPECT_ANY_THROW(SimulationSocket second(path));

    std::filesystem::remove(path);
}

TEST(SimulationSocket, TryCreateReturnsNullWhenLiveHostExists) {
    const std::filesystem::path path = unique_socket_path(5);
    std::filesystem::remove(path);

    auto host = SimulationSocket::try_create(path);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(SimulationSocket::try_create(path), nullptr);  // live host -> null, no throw

    // The throwaway object from the failed try_create above must not remove the live
    // host's socket on destruction (ownership-gated teardown).
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(can_connect(path));

    std::filesystem::remove(path);
}

TEST(SimulationSocket, TryCreateReclaimsStaleSocket) {
    const std::filesystem::path path = unique_socket_path(6);
    leave_stale_socket(path);

    auto host = SimulationSocket::try_create(path);
    EXPECT_NE(host, nullptr);  // stale leftover reclaimed
    EXPECT_TRUE(can_connect(path));

    std::filesystem::remove(path);
}

TEST(SimulationSocket, StartServingHandlesRequests) {
    const std::filesystem::path path = unique_socket_path(7);
    std::filesystem::remove(path);

    auto server = SimulationSocket::try_create(path);
    ASSERT_NE(server, nullptr);
    // Handler reverses the request bytes, so the response is distinguishable from a plain echo.
    server->start_serving(
        [](const std::vector<uint8_t>& request) { return std::vector<uint8_t>(request.rbegin(), request.rend()); });

    const int fd = connect_fd(path);
    ASSERT_GE(fd, 0);

    const std::vector<uint8_t> request = {1, 2, 3, 4};
    ASSERT_TRUE(send_framed(fd, request));
    const auto response = recv_framed(fd);

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(*response, std::vector<uint8_t>({4, 3, 2, 1}));

    ::close(fd);
    std::filesystem::remove(path);
}

TEST(SimulationSocket, DefaultSocketPathIsPerChip) {
    EXPECT_NE(SimulationSocket::default_socket_path(0), SimulationSocket::default_socket_path(1));
}

TEST(SimulationSocket, DefaultSocketPathIsAbsolute) {
    EXPECT_TRUE(SimulationSocket::default_socket_path(0).is_absolute());
}
