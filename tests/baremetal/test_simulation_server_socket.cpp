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
    const std::filesystem::path dir = "/tmp/tt-umd-sim-server-0";
    EXPECT_NE(SimulationServerSocket::default_socket_path(dir, 0), SimulationServerSocket::default_socket_path(dir, 1));
}

TEST(SimulationServerSocket, DefaultSocketPathIsInServerDirectory) {
    const std::filesystem::path dir = "/tmp/tt-umd-sim-server-3";
    EXPECT_EQ(SimulationServerSocket::default_socket_path(dir, 0).parent_path(), dir);
}

// allocate_server_directory hands each caller a distinct, freshly created directory, and each shows
// up in list_server_directories keyed by the index parsed out of its name.
TEST(SimulationServerSocket, AllocateServerDirectoryClaimsDistinctDirs) {
    namespace fs = std::filesystem;
    const fs::path a = SimulationServerSocket::allocate_server_directory();
    const fs::path b = SimulationServerSocket::allocate_server_directory();
    EXPECT_NE(a, b);
    EXPECT_TRUE(fs::is_directory(a));
    EXPECT_TRUE(fs::is_directory(b));

    const auto index_a = SimulationServerSocket::server_index_from_directory_path(a);
    ASSERT_TRUE(index_a.has_value());
    EXPECT_EQ(SimulationServerSocket::list_server_directories().count(*index_a), 1u);

    fs::remove(a);
    fs::remove(b);
}

// server_index_from_directory_path is the inverse of allocate_server_directory's naming; it must
// accept exactly the tt-umd-sim-server-<index> convention and reject anything else.
TEST(SimulationServerSocket, ServerIndexFromDirectoryPathParsesConvention) {
    auto i0 = SimulationServerSocket::server_index_from_directory_path("tt-umd-sim-server-0");
    ASSERT_TRUE(i0.has_value());
    EXPECT_EQ(*i0, 0);

    auto i7 = SimulationServerSocket::server_index_from_directory_path("/some/dir/tt-umd-sim-server-7");
    ASSERT_TRUE(i7.has_value());
    EXPECT_EQ(*i7, 7);

    // Non-matching names yield nullopt.
    EXPECT_FALSE(SimulationServerSocket::server_index_from_directory_path("tt-umd-sim-server-").has_value());
    EXPECT_FALSE(SimulationServerSocket::server_index_from_directory_path("tt-umd-sim-server-x").has_value());
    EXPECT_FALSE(SimulationServerSocket::server_index_from_directory_path("tt-umd-sim-0.sock").has_value());
    EXPECT_FALSE(SimulationServerSocket::server_index_from_directory_path("foo").has_value());
}

// chip_id_from_socket_path is the inverse of default_socket_path's naming; it must accept exactly
// the tt-umd-sim-<id>.sock convention and reject anything else.
TEST(SimulationServerSocket, ChipIdFromSocketPathParsesConvention) {
    auto id0 = SimulationServerSocket::chip_id_from_socket_path("tt-umd-sim-0.sock");
    ASSERT_TRUE(id0.has_value());
    EXPECT_EQ(*id0, 0);

    auto id7 = SimulationServerSocket::chip_id_from_socket_path("/some/dir/tt-umd-sim-7.sock");
    ASSERT_TRUE(id7.has_value());
    EXPECT_EQ(*id7, 7);

    // Round-trips default_socket_path().
    auto id3 = SimulationServerSocket::chip_id_from_socket_path(
        SimulationServerSocket::default_socket_path("/tmp/tt-umd-sim-server-0", 3));
    ASSERT_TRUE(id3.has_value());
    EXPECT_EQ(*id3, 3);

    // Non-matching names yield nullopt (not a wrong id).
    EXPECT_FALSE(SimulationServerSocket::chip_id_from_socket_path("foo.sock").has_value());
    EXPECT_FALSE(SimulationServerSocket::chip_id_from_socket_path("tt-umd-sim-.sock").has_value());
    EXPECT_FALSE(SimulationServerSocket::chip_id_from_socket_path("tt-umd-sim-x.sock").has_value());
    EXPECT_FALSE(SimulationServerSocket::chip_id_from_socket_path("tt-umd-sim-1.txt").has_value());
}

// sockets_in_directory returns exactly the per-chip AF_UNIX sockets, keyed by chip id, ignoring
// non-sockets and sockets that don't match the naming convention.
TEST(SimulationServerSocket, SocketsInDirectoryPicksPerChipSockets) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("tt-umd-sim-dir-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    leave_stale_socket(dir / "tt-umd-sim-0.sock");                   // per-chip socket -> included
    leave_stale_socket(dir / "tt-umd-sim-2.sock");                   // per-chip socket -> included
    leave_stale_socket(dir / "unrelated.sock");                      // socket, wrong name -> excluded
    { std::ofstream(dir / "tt-umd-sim-9.sock") << "not a socket"; }  // right name, not a socket -> excluded

    const auto sockets = SimulationServerSocket::sockets_in_directory(dir);
    EXPECT_EQ(sockets.size(), 2u);
    ASSERT_EQ(sockets.count(0), 1u);
    ASSERT_EQ(sockets.count(2), 1u);
    EXPECT_EQ(sockets.at(0), dir / "tt-umd-sim-0.sock");
    EXPECT_EQ(sockets.at(2), dir / "tt-umd-sim-2.sock");

    fs::remove_all(dir);
}

TEST(SimulationServerSocket, SocketsInDirectoryEmptyWhenNoneOrNotADir) {
    namespace fs = std::filesystem;
    EXPECT_TRUE(SimulationServerSocket::sockets_in_directory("/nonexistent/tt-umd-xyz").empty());

    const fs::path dir = fs::temp_directory_path() / ("tt-umd-sim-empty-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    EXPECT_TRUE(SimulationServerSocket::sockets_in_directory(dir).empty());
    fs::remove_all(dir);
}
