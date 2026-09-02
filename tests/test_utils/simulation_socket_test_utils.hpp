// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace test_utils {

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

}  // namespace test_utils
