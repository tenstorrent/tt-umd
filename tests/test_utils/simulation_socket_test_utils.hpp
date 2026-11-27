// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <grp.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <utility>

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

// Removes a simulation server directory and everything left in it when the scope ends. A directory
// survives a test only when something in it is unowned -- a stale socket no host will tear down --
// and a remove_all() on the last line of the test body is exactly what a fatal assertion skips,
// since GTEST_ASSERT_* returns from the body. Leftovers are not inert: they enumerate in
// list_servers() and are what classify() now rejects, so one bailed-out test poisons the next run.
class ScopedServerDirectory {
public:
    explicit ScopedServerDirectory(std::filesystem::path directory) : directory_(std::move(directory)) {}

    ScopedServerDirectory(const ScopedServerDirectory&) = delete;
    ScopedServerDirectory& operator=(const ScopedServerDirectory&) = delete;

    // Non-throwing overload: a destructor is implicitly noexcept, so a filesystem_error would call
    // std::terminate. Cleanup is best-effort.
    ~ScopedServerDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
    }

    const std::filesystem::path& path() const { return directory_; }

private:
    std::filesystem::path directory_;
};

// Returns whether callable throws a std::exception; its result, if any, is discarded.
template <typename Callable>
bool throws(Callable&& callable) {
    try {
        static_cast<void>(std::forward<Callable>(callable)());
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

// Returns whether check held; a check that throws did not hold.
inline bool holds(const std::function<bool()>& check) {
    try {
        return check();
    } catch (...) {
        return false;
    }
}

// Runs check in a forked child that has dropped to uid/gid 65534 (nobody) with no supplementary
// groups and returns whether it held. Root bypasses file permissions, so this is how a root process
// (as CI runs) sees a path the way another user does. The child reports through its exit status only
// and leaves via _exit, so it never tears down state it shares with the parent. Anything other than
// the check running and not holding is an environment problem, reported as a test failure so it is
// not mistaken for a regression.
inline bool holds_as_nobody(const std::function<bool()>& check) {
    constexpr int held_status = 0;
    constexpr int not_held_status = 1;
    constexpr int drop_failed_status = 2;
    const pid_t pid = ::fork();
    const int fork_errno = errno;
    if (pid == 0) {
        constexpr uid_t nobody = 65534;
        // Supplementary groups first and uid last: each step needs the privileges the next one drops.
        if (::setgroups(0, nullptr) != 0 || ::setgid(nobody) != 0 || ::setuid(nobody) != 0) {
            ::_exit(drop_failed_status);
        }
        ::_exit(holds(check) ? held_status : not_held_status);
    }
    if (pid < 0) {
        ADD_FAILURE() << "fork failed: " << std::strerror(fork_errno);
        return false;
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {
        const int wait_errno = errno;
        ADD_FAILURE() << "waitpid failed: " << std::strerror(wait_errno);
        return false;
    }
    if (WIFSIGNALED(status)) {
        ADD_FAILURE() << "check child killed by signal " << WTERMSIG(status);
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) == drop_failed_status) {
        ADD_FAILURE() << "check child could not drop to uid/gid 65534";
        return false;
    }
    return WEXITSTATUS(status) == held_status;
}

// Runs check with path's permission bits set to mode and restores them afterwards: as nobody when
// running as root (see holds_as_nobody), in this process otherwise, where the mode alone denies
// this user what the test needs denied. Returns whether check held; a check that throws did not.
inline bool holds_with_mode(
    const std::filesystem::path& path, std::filesystem::perms mode, const std::function<bool()>& check) {
    // Restores from a destructor so a fatal path out of this scope cannot leave path locked down.
    // check may have removed path (the very thing a test guards against); restore what is there.
    class RestoreMode {
    public:
        explicit RestoreMode(const std::filesystem::path& path) :
            path_(path), previous_(std::filesystem::status(path).permissions()) {}

        RestoreMode(const RestoreMode&) = delete;
        RestoreMode& operator=(const RestoreMode&) = delete;

        ~RestoreMode() {
            std::error_code ec;
            std::filesystem::permissions(path_, previous_, ec);
        }

    private:
        const std::filesystem::path& path_;
        std::filesystem::perms previous_;
    };

    const RestoreMode restore(path);
    std::filesystem::permissions(path, mode);
    return ::geteuid() == 0 ? holds_as_nobody(check) : holds(check);
}

// Runs check as a user who may not connect to the socket at socket_path (connect() needs write
// permission on the socket file), which is how another user meets an owner-only socket. The socket's
// directory must be searchable by uid 65534 for the root path to reach it.
inline bool holds_for_a_user_who_may_not_connect(
    const std::filesystem::path& socket_path, const std::function<bool()>& check) {
    return holds_with_mode(socket_path, std::filesystem::perms::owner_read, check);
}

// Runs check as a user who may not list directory, which is how another user meets an owner-only
// server directory.
inline bool holds_for_a_user_who_may_not_read(
    const std::filesystem::path& directory, const std::function<bool()>& check) {
    return holds_with_mode(
        directory, ::geteuid() == 0 ? std::filesystem::perms::owner_all : std::filesystem::perms::none, check);
}

}  // namespace tt::umd::test_utils
