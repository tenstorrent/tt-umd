// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <asio.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "simulation/simulation_server_transport.hpp"

using namespace tt::umd;
using stream_protocol = asio::local::stream_protocol;

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

// Connects an asio UNIX socket (blocking) to the server at path, for framed transport I/O. Shares
// the caller's io_context; the socket closes on destruction.
stream_protocol::socket connect_client(asio::io_context& io, const std::filesystem::path& path) {
    stream_protocol::socket socket(io);
    socket.connect(stream_protocol::endpoint(path.string()));
    socket.native_non_blocking(false);
    return socket;
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

// Inverts every byte, so a served reply is distinguishable from the request that produced it.
std::vector<uint8_t> invert(const std::vector<uint8_t>& request) {
    std::vector<uint8_t> reply = request;
    for (uint8_t& byte : reply) {
        byte = static_cast<uint8_t>(~byte);
    }
    return reply;
}

// With a handler, a framed request is served and the framed reply comes back transformed.
TEST_F(SimulationServerSocketTest, ServesRequestsThroughHandler) {
    auto server = SimulationServerSocket::create(path_);
    server->serve(invert);
    asio::io_context io;
    stream_protocol::socket client = connect_client(io, path_);

    const std::vector<uint8_t> request = {0x01, 0x02, 0x03};
    send_framed(client, request);

    EXPECT_EQ(recv_framed(client), invert(request));
}

// One connection carries many request/reply turns in sequence.
TEST_F(SimulationServerSocketTest, HandlesSequentialRequestsOnOneConnection) {
    auto server = SimulationServerSocket::create(path_);
    server->serve([](const std::vector<uint8_t>& request) { return request; });
    asio::io_context io;
    stream_protocol::socket client = connect_client(io, path_);

    for (uint8_t i = 0; i < 5; ++i) {
        const std::vector<uint8_t> request = {i, static_cast<uint8_t>(i + 1)};
        send_framed(client, request);
        EXPECT_EQ(recv_framed(client), request);
    }
}

// Two clients are served concurrently (a thread per connection); neither sees the other's data.
TEST_F(SimulationServerSocketTest, SupportsMultipleClients) {
    auto server = SimulationServerSocket::create(path_);
    server->serve([](const std::vector<uint8_t>& request) { return request; });
    asio::io_context io;
    stream_protocol::socket first = connect_client(io, path_);
    stream_protocol::socket second = connect_client(io, path_);

    send_framed(first, {0xAA});
    send_framed(second, {0xBB});
    EXPECT_EQ(recv_framed(first), (std::vector<uint8_t>{0xAA}));
    EXPECT_EQ(recv_framed(second), (std::vector<uint8_t>{0xBB}));
}

// Destroying the server while a client is still connected must not hang: the serving thread,
// blocked in recv_framed, is unblocked and joined during teardown. The client then sees EOF.
TEST_F(SimulationServerSocketTest, TearsDownCleanlyWithClientConnected) {
    asio::io_context io;
    stream_protocol::socket client(io);
    {
        auto server = SimulationServerSocket::create(path_);
        server->serve([](const std::vector<uint8_t>& request) { return request; });
        client = connect_client(io, path_);
        // One round-trip so the serving thread is up and then blocked awaiting the next request.
        send_framed(client, {0x42});
        EXPECT_EQ(recv_framed(client), (std::vector<uint8_t>{0x42}));
    }  // server destroyed here -- must join the blocked serving thread without hanging.

    EXPECT_THROW(recv_framed(client), std::exception);  // the host shut the connection down
}

// A socket bound without a handler is presence-only -- connectable (liveness) but not accepting.
// serve() installs the handler and starts accepting, so serving can begin after the backend is
// ready. This is the path the host device uses (create the socket, then serve once up).
TEST_F(SimulationServerSocketTest, ServesAfterDeferredServe) {
    auto server = SimulationServerSocket::create(path_);  // presence-only: no handler yet
    EXPECT_TRUE(can_connect(path_));                      // bound + connectable

    server->serve([](const std::vector<uint8_t>& request) { return request; });  // echo

    asio::io_context io;
    stream_protocol::socket client = connect_client(io, path_);
    send_framed(client, {0x07, 0x08});
    EXPECT_EQ(recv_framed(client), (std::vector<uint8_t>{0x07, 0x08}));
}

TEST(SimulationServerSocket, DefaultSocketPathIsPerChip) {
    EXPECT_NE(SimulationServerSocket::default_socket_path(0), SimulationServerSocket::default_socket_path(1));
}

TEST(SimulationServerSocket, DefaultSocketPathIsAbsolute) {
    EXPECT_TRUE(SimulationServerSocket::default_socket_path(0).is_absolute());
}
