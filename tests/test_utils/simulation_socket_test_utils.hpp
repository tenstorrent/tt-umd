// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "simulation/simulation_server_socket.hpp"

namespace tt::umd::test_utils {

// Leaves a socket file at path that nothing is serving: bound, never listened on, and closed --
// exactly what a crashed simulation host leaves on disk. connect() to it yields ECONNREFUSED, so it
// is indistinguishable from a live host's socket by stat() alone, which is what makes it useful for
// exercising liveness-aware code paths.
inline void leave_stale_socket(const std::filesystem::path& path) {
    const std::string path_str = path.string();
    // sun_path is fixed-size and strncpy truncates silently, which would bind some other path;
    // refuse rather than test the wrong thing.
    ASSERT_LT(path_str.size(), sizeof(sockaddr_un::sun_path)) << "socket path too long for sockaddr_un: " << path_str;
    // A leftover from an earlier run would make bind() fail with EADDRINUSE.
    std::filesystem::remove(path);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_str.c_str(), path_str.size() + 1);
    const int bind_result = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    // Closed before asserting, so a failed bind doesn't leak the descriptor out of the helper.
    ::close(fd);
    ASSERT_EQ(bind_result, 0);
    ASSERT_TRUE(std::filesystem::exists(path));
}

// A server directory allocated by a child process, which holds its claim until kill_owner() (or
// destruction) -- standing in for another host on the machine and, once killed, for one that died
// without tearing anything down.
class ServerDirectoryOfAnotherProcess {
public:
    ServerDirectoryOfAnotherProcess() {
        int from_owner[2];
        if (::pipe(from_owner) != 0) {
            throw std::runtime_error("pipe() failed");
        }
        owner_ = ::fork();
        if (owner_ < 0) {
            throw std::runtime_error("fork() failed");
        }
        if (owner_ == 0) {
            // Dies with the test, so a crashed run does not leave an owner holding the claim forever.
            ::prctl(PR_SET_PDEATHSIG, SIGKILL);
            ::close(from_owner[0]);
            try {
                const std::string directory = SimulationServerSocket::allocate_server_directory().string();
                [[maybe_unused]] const ssize_t sent = ::write(from_owner[1], directory.data(), directory.size());
            } catch (...) {
                ::_exit(1);
            }
            ::close(from_owner[1]);
            while (true) {
                ::pause();
            }
        }
        ::close(from_owner[1]);
        std::string directory;
        char buffer[256];
        ssize_t received = 0;
        while ((received = ::read(from_owner[0], buffer, sizeof(buffer))) > 0) {
            directory.append(buffer, received);
        }
        ::close(from_owner[0]);
        directory_ = directory;
    }

    ServerDirectoryOfAnotherProcess(const ServerDirectoryOfAnotherProcess&) = delete;
    ServerDirectoryOfAnotherProcess& operator=(const ServerDirectoryOfAnotherProcess&) = delete;

    ~ServerDirectoryOfAnotherProcess() { kill_owner(); }

    // SIGKILLs the owning process and reaps it, so its claim is released by the time this returns.
    void kill_owner() {
        if (owner_ > 0) {
            ::kill(owner_, SIGKILL);
            ::waitpid(owner_, nullptr, 0);
            owner_ = -1;
        }
    }

    const std::filesystem::path& path() const { return directory_; }

private:
    pid_t owner_ = -1;
    std::filesystem::path directory_;
};

}  // namespace tt::umd::test_utils
