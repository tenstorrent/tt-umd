// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <asio.hpp>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "simulation/simulation_server_transport.hpp"
#include "tests/test_utils/simulation_socket_test_utils.hpp"

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

// A host that is listening but never accepts, with its accept queue already full: what a host whose
// accept loop has stalled looks like from outside. Raw POSIX rather than SimulationServerSocket,
// since filling the queue takes a backlog small enough to reach with a handful of connections.
class StalledListener {
public:
    explicit StalledListener(const std::filesystem::path& path) {
        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const sockaddr_un addr = make_address(path);
        // Backlog 0: the kernel queues a single unaccepted connection and refuses the next.
        listening_ = listen_fd_ >= 0 &&
                     ::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0 &&
                     ::listen(listen_fd_, 0) == 0;
        if (!listening_) {
            return;
        }
        // Queue non-blocking connects nobody accepts until the kernel reports the queue full.
        for (int attempt = 0; attempt < max_queued_connections && !accept_queue_full_; ++attempt) {
            const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                return;
            }
            queued_fds_.push_back(fd);
            if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
                accept_queue_full_ = errno == EAGAIN;
                if (!accept_queue_full_) {
                    return;
                }
            }
        }
    }

    StalledListener(const StalledListener&) = delete;
    StalledListener& operator=(const StalledListener&) = delete;

    ~StalledListener() {
        close_listener();
        for (int fd : queued_fds_) {
            ::close(fd);
        }
    }

    // Listening, and the accept queue refuses further connections.
    bool accept_queue_full() const { return listening_ && accept_queue_full_; }

    // Closing the listener wakes any connect() blocked on its queue.
    void close_listener() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

private:
    static constexpr int max_queued_connections = 16;

    static sockaddr_un make_address(const std::filesystem::path& path) {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        return addr;
    }

    int listen_fd_ = -1;
    bool listening_ = false;
    bool accept_queue_full_ = false;
    std::vector<int> queued_fds_;
};

// The permission bits of path (setuid/setgid/sticky included), for comparing against octal modes.
unsigned mode_of(const std::filesystem::path& path) {
    return static_cast<unsigned>(std::filesystem::status(path).permissions()) & 07777;
}

// Sets the process umask for the scope and restores the previous one at its end, so a test can
// check that a mode holds whatever umask the user runs with.
class ScopedUmask {
public:
    explicit ScopedUmask(mode_t mask) : previous_(::umask(mask)) {}

    ScopedUmask(const ScopedUmask&) = delete;
    ScopedUmask& operator=(const ScopedUmask&) = delete;

    ~ScopedUmask() { ::umask(previous_); }

private:
    mode_t previous_;
};

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
    test_utils::leave_stale_socket(path_);
    EXPECT_FALSE(can_connect(path_));

    auto server = SimulationServerSocket::create(path_);

    EXPECT_TRUE(can_connect(path_));
}

TEST_F(SimulationServerSocketTest, ThrowsWhenLiveServerAlreadyExists) {
    auto server = SimulationServerSocket::create(path_);

    EXPECT_ANY_THROW(SimulationServerSocket::create(path_));
}

// A failed create() builds and destroys a socket object that never won the bind; ownership-gated
// teardown must stop it removing the live host's socket on the way out.
TEST_F(SimulationServerSocketTest, FailedCreateLeavesTheLiveHostAlone) {
    auto host = SimulationServerSocket::create(path_);

    EXPECT_ANY_THROW(SimulationServerSocket::create(path_));

    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_TRUE(SimulationServerSocket::is_live(path_));
}

// A non-socket file squatting the path also yields EADDRINUSE on bind; the reclaim path
// must refuse to delete it instead of destroying unrelated data.
TEST_F(SimulationServerSocketTest, RefusesToReclaimNonSocketFile) {
    { std::ofstream(path_) << "not a socket"; }

    EXPECT_THROW(SimulationServerSocket::create(path_), std::exception);
    EXPECT_TRUE(std::filesystem::exists(path_));  // the regular file was left untouched
}

// is_live() is the liveness question the socket file's presence cannot answer: nothing there and a
// socket left by a crashed owner both read as "no host", only a bound-and-listening owner as live.
TEST_F(SimulationServerSocketTest, IsLiveDistinguishesAHostFromAnAbsentOrStaleSocket) {
    EXPECT_FALSE(SimulationServerSocket::is_live(path_));  // nothing there at all

    test_utils::leave_stale_socket(path_);
    ASSERT_TRUE(std::filesystem::exists(path_));           // the file is on disk...
    EXPECT_FALSE(SimulationServerSocket::is_live(path_));  // ...but nothing is serving on it

    auto host = SimulationServerSocket::create(path_);  // reclaims the stale file and binds
    EXPECT_TRUE(SimulationServerSocket::is_live(path_));
}

// A host whose accept queue is full still holds the path, so it reads as live -- and the probe must
// return rather than wait for a queue slot the host may never free.
TEST_F(SimulationServerSocketTest, IsLiveReturnsTrueForAHostWhoseAcceptQueueIsFull) {
    StalledListener host(path_);
    ASSERT_TRUE(host.accept_queue_full());

    auto probe = std::async(std::launch::async, [this] { return SimulationServerSocket::is_live(path_); });
    const bool returned = probe.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    if (!returned) {
        host.close_listener();  // unblocks the probe, so the test fails instead of hanging
    }

    ASSERT_TRUE(returned) << "is_live() blocked on a listener whose accept queue is full";
    EXPECT_TRUE(probe.get());
}

// A path too long for sockaddr_un cannot name a reachable listener, so is_live() reports false
// rather than throwing.
TEST_F(SimulationServerSocketTest, IsLiveIsFalseForAnOverlongPath) {
    const std::filesystem::path too_long =
        std::filesystem::temp_directory_path() / (std::string(sizeof(sockaddr_un::sun_path), 'x') + ".sock");
    EXPECT_FALSE(SimulationServerSocket::is_live(too_long));
}

// A server is private to its user: under the most permissive umask, the socket file is still
// connectable only by its owner.
TEST_F(SimulationServerSocketTest, SocketFileIsOwnerOnly) {
    const ScopedUmask permissive_umask(0);

    auto server = SimulationServerSocket::create(path_);

    EXPECT_EQ(mode_of(path_), 0600u);
}

// A parent directory create() has to make itself is owner-only, so other users cannot even reach
// the socket inside it.
TEST_F(SimulationServerSocketTest, CreatesAMissingParentDirectoryOwnerOnly) {
    const test_utils::ScopedServerDirectory parent(
        std::filesystem::temp_directory_path() / ("tt-umd-sim-parent-test-" + std::to_string(::getpid())));
    std::filesystem::remove_all(parent.path());
    const ScopedUmask permissive_umask(0);

    auto server = SimulationServerSocket::create(parent.path() / "tt-umd-sim-0.sock");

    EXPECT_EQ(mode_of(parent.path()), 0700u);
}

// Every directory create() makes on the way to the socket is owner-only, not only the socket's own.
TEST_F(SimulationServerSocketTest, CreatesMissingAncestorDirectoriesOwnerOnly) {
    const test_utils::ScopedServerDirectory ancestor(
        std::filesystem::temp_directory_path() / ("tt-umd-sim-ancestor-test-" + std::to_string(::getpid())));
    std::filesystem::remove_all(ancestor.path());
    const std::filesystem::path parent = ancestor.path() / "nested";
    const ScopedUmask permissive_umask(0);

    auto server = SimulationServerSocket::create(parent / "tt-umd-sim-0.sock");

    EXPECT_EQ(mode_of(ancestor.path()), 0700u);
    EXPECT_EQ(mode_of(parent), 0700u);
}

// A parent directory that already exists belongs to whoever made it; create() does not change it.
TEST_F(SimulationServerSocketTest, LeavesAnExistingParentDirectoryAsItIs) {
    const test_utils::ScopedServerDirectory parent(
        std::filesystem::temp_directory_path() / ("tt-umd-sim-parent-test-" + std::to_string(::getpid())));
    std::filesystem::remove_all(parent.path());
    std::filesystem::create_directory(parent.path());
    std::filesystem::permissions(parent.path(), static_cast<std::filesystem::perms>(0755));

    auto server = SimulationServerSocket::create(parent.path() / "tt-umd-sim-0.sock");

    EXPECT_EQ(mode_of(parent.path()), 0755u);
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

// The directory allocate_server_directory() claims is owner-only (0700) whatever umask the caller
// runs with: a permissive umask must not widen it, and a restrictive one must not narrow it below
// what its owner needs to serve in it.
class AllocateServerDirectoryUmaskTest : public ::testing::TestWithParam<mode_t> {};

TEST_P(AllocateServerDirectoryUmaskTest, ClaimsAnOwnerOnlyDirectory) {
    const ScopedUmask umask(GetParam());

    const test_utils::ScopedServerDirectory directory(SimulationServerSocket::allocate_server_directory());

    EXPECT_EQ(mode_of(directory.path()), 0700u);
}

INSTANTIATE_TEST_SUITE_P(
    SimulationServerSocket,
    AllocateServerDirectoryUmaskTest,
    ::testing::Values(0000, 0022, 0777),
    [](const ::testing::TestParamInfo<mode_t>& info) {
        std::ostringstream name;
        name << "Umask" << std::oct << info.param;
        return name.str();
    });

// list_server_directories must not follow a symlink: the temp dir is world-writable, so a link named
// like a server directory (pointing anywhere) must not enumerate as a server.
TEST(SimulationServerSocket, ListServerDirectoriesIgnoresSymlinks) {
    namespace fs = std::filesystem;
    constexpr int index = 9042;  // distinctive, unlikely to collide with a live server
    const fs::path link = fs::temp_directory_path() / ("tt-umd-sim-server-" + std::to_string(index));
    const fs::path target = fs::temp_directory_path() / ("tt-umd-sim-symlink-target-" + std::to_string(::getpid()));
    fs::remove(link);
    fs::create_directory(target);
    fs::create_directory_symlink(target, link);

    EXPECT_EQ(SimulationServerSocket::list_server_directories().count(index), 0u);

    fs::remove(link);
    fs::remove(target);
}

// list_server_directories enumerates the calling user's servers only: a directory named like a
// server but owned by someone else is not one of them. Giving a directory another owner needs root,
// which the CI containers run as.
TEST(SimulationServerSocket, ListServerDirectoriesSkipsOtherUsersDirectories) {
    if (::geteuid() != 0) {
        GTEST_SKIP() << "needs root to give a directory another owner";
    }
    namespace fs = std::filesystem;
    const int index = 9043;  // distinctive, unlikely to collide with a live server
    const test_utils::ScopedServerDirectory directory(
        fs::temp_directory_path() / ("tt-umd-sim-server-" + std::to_string(index)));
    fs::create_directory(directory.path());
    const uid_t nobody = 65534;
    ASSERT_EQ(::chown(directory.path().c_str(), nobody, nobody), 0);

    EXPECT_EQ(SimulationServerSocket::list_server_directories().count(index), 0u);
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

    // Leading zeros are rejected so "-0" and "-00" can't both map to chip 0 and collide.
    EXPECT_FALSE(SimulationServerSocket::chip_id_from_socket_path("tt-umd-sim-00.sock").has_value());
    EXPECT_FALSE(SimulationServerSocket::chip_id_from_socket_path("tt-umd-sim-01.sock").has_value());
}

// sockets_in_directory returns exactly the per-chip AF_UNIX sockets, keyed by chip id, ignoring
// non-sockets and sockets that don't match the naming convention.
TEST(SimulationServerSocket, SocketsInDirectoryPicksPerChipSockets) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("tt-umd-sim-dir-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    test_utils::leave_stale_socket(dir / "tt-umd-sim-0.sock");       // per-chip socket -> included
    test_utils::leave_stale_socket(dir / "tt-umd-sim-2.sock");       // per-chip socket -> included
    test_utils::leave_stale_socket(dir / "unrelated.sock");          // socket, wrong name -> excluded
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

}  // namespace
