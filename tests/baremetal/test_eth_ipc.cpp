// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>

#include <filesystem>
#include <fstream>
#include <functional>

#include "simulation/eth_ipc.hpp"

namespace {
using tt::umd::EthIpcEndpoint;
using namespace std::chrono_literals;

class EthIpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        char path[] = "/tmp/umd-eth-ipc-test-XXXXXX";
        ASSERT_NE(::mkdtemp(path), nullptr);
        directory = path;
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    std::string directory;
};

size_t open_fd_count() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{});
}

pid_t spawn(const std::function<void()>& action) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::alarm(5);
        try {
            action();
            ::_exit(0);
        } catch (const std::exception& error) {
            ::dprintf(STDERR_FILENO, "IPC child: %s\n", error.what());
            ::_exit(1);
        }
    }
    return pid;
}

void expect_success(pid_t pid) {
    ASSERT_GT(pid, 0);
    int status = 0;
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

void exchange(EthIpcEndpoint& endpoint, char outgoing, char expected) {
    if (::write(endpoint.write_fd(), &outgoing, 1) != 1) {
        throw std::runtime_error("payload write failed");
    }
    const auto deadline = EthIpcEndpoint::Clock::now() + 2s;
    char incoming = 0;
    while (::read(endpoint.read_fd(), &incoming, 1) != 1) {
        if (EthIpcEndpoint::Clock::now() >= deadline) {
            throw std::runtime_error("payload read timed out");
        }
        std::this_thread::sleep_for(1ms);
    }
    if (incoming != expected) {
        throw std::runtime_error("payload reached wrong session");
    }
}

TEST_F(EthIpcTest, TwoProcessesHandshakeWithDelayedIncomingWriterAndExchange) {
    const size_t before = open_fd_count();
    const pid_t child = spawn([&] {
        EthIpcEndpoint peer(directory, "b", "a");
        // Give the other rank time to connect/send while our incoming FIFO has no writer.
        std::this_thread::sleep_for(50ms);
        const auto deadline = EthIpcEndpoint::Clock::now() + 2s;
        peer.connect(deadline);
        EthIpcEndpoint::handshake({&peer}, deadline);
        exchange(peer, 'b', 'a');
    });
    ASSERT_GT(child, 0);
    EXPECT_NO_THROW({
        EthIpcEndpoint endpoint(directory, "a", "b");
        const auto deadline = EthIpcEndpoint::Clock::now() + 2s;
        endpoint.connect(deadline);
        EthIpcEndpoint::handshake({&endpoint}, deadline);
        exchange(endpoint, 'a', 'b');
    });
    expect_success(child);
    EXPECT_TRUE(std::filesystem::is_empty(directory));
    EXPECT_EQ(open_fd_count(), before);
}

TEST_F(EthIpcTest, RejectsStaleReceiveNodesWithoutUnlinkingThem) {
    const auto path = directory + "/a";
    const size_t before = open_fd_count();
    ASSERT_EQ(::mkfifo(path.c_str(), 0600), 0);
    EXPECT_THROW(EthIpcEndpoint(directory, "a", "b"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_fifo(path));
    std::filesystem::remove(path);
    std::ofstream(path) << "keep";
    EXPECT_THROW(EthIpcEndpoint(directory, "a", "b"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
    std::filesystem::remove(path);
    ASSERT_EQ(::symlink("missing", path.c_str()), 0);
    EXPECT_THROW(EthIpcEndpoint(directory, "a", "b"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_symlink(path));
    EXPECT_EQ(open_fd_count(), before);
}

TEST_F(EthIpcTest, RejectsUnsafeDirectoryAndPeerNodes) {
    const size_t before = open_fd_count();
    ASSERT_EQ(::chmod(directory.c_str(), 0755), 0);
    EXPECT_THROW(EthIpcEndpoint(directory, "a", "b"), std::runtime_error);
    ASSERT_EQ(::chmod(directory.c_str(), 0700), 0);
    const auto alias = directory + "/alias";
    ASSERT_EQ(::symlink(directory.c_str(), alias.c_str()), 0);
    EXPECT_THROW(EthIpcEndpoint(alias, "a", "b"), std::runtime_error);
    const auto peer = directory + "/b";
    std::ofstream(peer) << "keep";
    {
        EthIpcEndpoint endpoint(directory, "a", "b");
        EXPECT_THROW(endpoint.connect(EthIpcEndpoint::Clock::now() + 100ms), std::runtime_error);
    }
    std::filesystem::remove(peer);
    ASSERT_EQ(::symlink("missing", peer.c_str()), 0);
    {
        EthIpcEndpoint endpoint(directory, "a", "b");
        EXPECT_THROW(endpoint.connect(EthIpcEndpoint::Clock::now() + 100ms), std::runtime_error);
    }
    std::filesystem::remove(peer);
    ASSERT_EQ(::mkfifo(peer.c_str(), 0600), 0);
    ASSERT_EQ(::chmod(peer.c_str(), 0644), 0);
    const int reader = ::open(peer.c_str(), O_RDONLY | O_NONBLOCK);
    ASSERT_GE(reader, 0);
    {
        EthIpcEndpoint endpoint(directory, "a", "b");
        EXPECT_THROW(endpoint.connect(EthIpcEndpoint::Clock::now() + 100ms), std::runtime_error);
    }
    ::close(reader);
    EXPECT_EQ(open_fd_count(), before);
}

TEST_F(EthIpcTest, AbsentPeerHasBoundedDeadlineAndCleansUp) {
    const size_t before = open_fd_count();
    const auto start = EthIpcEndpoint::Clock::now();
    {
        EthIpcEndpoint endpoint(directory, "a", "missing");
        EXPECT_THROW(endpoint.connect(start + 30ms), std::runtime_error);
    }
    EXPECT_LT(EthIpcEndpoint::Clock::now() - start, 1s);
    EXPECT_TRUE(std::filesystem::is_empty(directory));
    EXPECT_EQ(open_fd_count(), before);
}

TEST_F(EthIpcTest, SameEndpointNamesInConcurrentSessionsStayIsolated) {
    const auto second = directory + "/second";
    ASSERT_EQ(::mkdir(second.c_str(), 0700), 0);
    std::vector<pid_t> children;
    for (const auto& session : {directory, second}) {
        children.push_back(spawn([&, session] {
            EthIpcEndpoint peer(session, "b", "a");
            const auto deadline = EthIpcEndpoint::Clock::now() + 2s;
            peer.connect(deadline);
            EthIpcEndpoint::handshake({&peer}, deadline);
            exchange(peer, session == directory ? 'B' : 'D', session == directory ? 'A' : 'C');
        }));
    }
    EXPECT_NO_THROW({
        EthIpcEndpoint first(directory, "a", "b");
        EthIpcEndpoint other(second, "a", "b");
        const auto deadline = EthIpcEndpoint::Clock::now() + 2s;
        first.connect(deadline);
        other.connect(deadline);
        EthIpcEndpoint::handshake({&first, &other}, deadline);
        exchange(first, 'A', 'B');
        exchange(other, 'C', 'D');
    });
    for (pid_t child : children) {
        expect_success(child);
    }
}

TEST_F(EthIpcTest, MacRegistryLockHonorsSetupDeadline) {
    const int holder = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    ASSERT_GE(holder, 0);
    ASSERT_EQ(::flock(holder, LOCK_EX), 0);
    {
        EthIpcEndpoint endpoint(directory, "a", "b");
        const auto start = EthIpcEndpoint::Clock::now();
        EXPECT_THROW(endpoint.reserve_mac(123, 0, start + 30ms), std::runtime_error);
        EXPECT_LT(EthIpcEndpoint::Clock::now() - start, 1s);
    }
    ::close(holder);
}

TEST_F(EthIpcTest, GlobalEndpointMacIsStableAndRejectsConflictingReservation) {
    const size_t before = open_fd_count();
    {
        EthIpcEndpoint first(directory, "a", "b");
        EthIpcEndpoint second(directory, "b", "a");
        constexpr uint64_t uid = 0x123456789abcdef0ULL;
        const uint64_t mac = first.reserve_mac(uid, 3);
        EXPECT_EQ(mac, second.reserve_mac(uid, 3));
        EXPECT_NE(mac, second.reserve_mac(uid + 1, 3));
        EXPECT_NE(mac, second.reserve_mac(uid, 4));
        EXPECT_EQ((mac >> 40) & 3, 2u);  // Locally administered unicast.
        const auto reservation = directory + "/mac_" + std::to_string(mac);
        // Inject a conflicting full identity at the same truncated MAC.
        std::ofstream(reservation, std::ios::trunc) << "different-global-endpoint";
        EXPECT_THROW(second.reserve_mac(uid, 3), std::runtime_error);
        std::filesystem::remove(reservation);
        ASSERT_EQ(::symlink("missing", reservation.c_str()), 0);
        EXPECT_THROW(first.reserve_mac(uid, 3), std::runtime_error);
    }
    EXPECT_EQ(open_fd_count(), before);
}

TEST_F(EthIpcTest, PeerExitDuringHandshakeThrowsWithoutDeliveringSigpipe) {
    // Run under SIG_DFL in a child so a SIGPIPE regression is reported, not fatal to the suite.
    const pid_t child = spawn([&] {
        ::signal(SIGPIPE, SIG_DFL);
        EthIpcEndpoint endpoint(directory, "a", "b");
        {
            EthIpcEndpoint peer(directory, "b", "a");
            endpoint.connect(EthIpcEndpoint::Clock::now() + 1s);
        }
        bool rejected = false;
        try {
            EthIpcEndpoint::handshake({&endpoint}, EthIpcEndpoint::Clock::now() + 100ms);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        if (!rejected) {
            throw std::runtime_error("peer exit was not rejected");
        }
        struct sigaction disposition {};
        ::sigaction(SIGPIPE, nullptr, &disposition);
        if (disposition.sa_handler != SIG_DFL) {
            throw std::runtime_error("SIGPIPE disposition changed");
        }
    });
    expect_success(child);
}
}  // namespace
