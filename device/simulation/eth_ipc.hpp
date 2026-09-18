// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace tt::umd {

// The launcher creates one private directory per job and distributes its path to
// every rank. It removes the directory only after all ranks have exited. Keeping
// it separate from simulation-server socket directories avoids their stale prune.
class EthIpcEndpoint {
public:
    using Clock = std::chrono::steady_clock;

    EthIpcEndpoint(const std::string& directory, std::string read_name, std::string write_name) :
        read_name_(std::move(read_name)), write_name_(std::move(write_name)) {
        dir_fd_ = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (dir_fd_ < 0) {
            fail("open private session directory " + directory);
        }
        try {
            struct stat st {};

            if (::fstat(dir_fd_, &st) != 0 || st.st_uid != ::geteuid() || (st.st_mode & 0777) != 0700) {
                throw std::runtime_error("TTSim Ethernet session directory must be owned by this user with mode 0700");
            }
            // Only the receiving endpoint creates/unlinks its FIFO. EEXIST is
            // an error: stale sessions and duplicate rank launches must not mix.
            if (::mkfifoat(dir_fd_, read_name_.c_str(), 0600) != 0) {
                fail("create receive FIFO " + read_name_);
            }
            created_ = true;
            read_fd_ = ::openat(dir_fd_, read_name_.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
            if (read_fd_ < 0) {
                fail("open receive FIFO " + read_name_);
            }
            validate_fifo(read_fd_);
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~EthIpcEndpoint() { cleanup(); }

    EthIpcEndpoint(const EthIpcEndpoint&) = delete;
    EthIpcEndpoint& operator=(const EthIpcEndpoint&) = delete;

    // The full endpoint identity is retained under a directory lock, so a
    // truncated hash collision is rejected rather than silently aliasing MACs.
    // Reservations live until launcher session cleanup, including after crashes.
    uint64_t reserve_mac(
        uint64_t uid, uint32_t channel, Clock::time_point deadline = Clock::now() + std::chrono::seconds(120)) {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned i = 0; i < 8; ++i) {
            hash = (hash ^ ((uid >> (8 * i)) & 0xff)) * 1099511628211ULL;
        }
        for (unsigned i = 0; i < 4; ++i) {
            hash = (hash ^ ((channel >> (8 * i)) & 0xff)) * 1099511628211ULL;
        }
        // Locally administered unicast; 06 also separates these from the 02
        // prefix used by in-process-only endpoints.
        const uint64_t mac = 0x060000000000ULL | (hash & 0xffffffffffULL);
        const std::string name = "mac_" + std::to_string(mac);
        const std::string identity = std::to_string(uid) + ":" + std::to_string(channel);
        while (::flock(dir_fd_, LOCK_EX | LOCK_NB) != 0) {
            if (errno != EWOULDBLOCK && errno != EINTR) {
                fail("lock session MAC registry");
            }
            wait_until(deadline, "waiting for session MAC registry");
        }
        int fd = -1;
        try {
            fd = ::openat(dir_fd_, name.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (fd < 0) {
                fail("open MAC reservation");
            }

            struct stat st {};

            if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid() ||
                (st.st_mode & 0777) != 0600 || st.st_nlink != 1) {
                throw std::runtime_error("TTSim Ethernet invalid MAC reservation file");
            }
            std::array<char, 64> existing{};
            const ssize_t size = ::read(fd, existing.data(), existing.size());
            if (size < 0) {
                fail("read MAC reservation");
            }
            if (size == 0) {
                if (::write(fd, identity.data(), identity.size()) != static_cast<ssize_t>(identity.size())) {
                    fail("write MAC reservation");
                }
            } else if (std::string(existing.data(), static_cast<size_t>(size)) != identity) {
                throw std::runtime_error("TTSim Ethernet MAC collision between distinct global endpoints");
            }
        } catch (...) {
            if (fd >= 0) {
                ::close(fd);
            }
            ::flock(dir_fd_, LOCK_UN);
            throw;
        }
        ::close(fd);
        ::flock(dir_fd_, LOCK_UN);
        return mac;
    }

    int read_fd() const { return read_fd_; }

    int write_fd() const { return write_fd_; }

    const std::string& peer_name() const { return write_name_; }

    void connect(Clock::time_point deadline) {
        while (write_fd_ < 0) {
            write_fd_ = ::openat(dir_fd_, write_name_.c_str(), O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
            if (write_fd_ >= 0) {
                validate_fifo(write_fd_);
                return;
            }
            if (errno != ENOENT && errno != ENXIO && errno != EINTR) {
                fail("open peer FIFO " + write_name_);
            }
            wait_until(deadline, "waiting for peer " + write_name_);
        }
    }

    // Call after all read ends and then all write ends have been opened. Each
    // readiness record fits atomically in a fresh FIFO; no rank waits for input
    // until it has sent every record. Simulator traffic starts only afterward.
    static void handshake(const std::vector<EthIpcEndpoint*>& links, Clock::time_point deadline) {
        constexpr std::array<char, 8> ready = {'T', 'T', 'S', 'I', 'M', 'F', 'D', '1'};
        for (auto* link : links) {
            // Suppress SIGPIPE for this write only; never alter process-wide
            // signal disposition in a library. send_ready implements this below.
            link->send_ready(ready.data(), ready.size());
        }
        for (auto* link : links) {
            std::array<char, ready.size()> received{};
            size_t offset = 0;
            while (offset < received.size()) {
                const ssize_t n = ::read(link->read_fd_, received.data() + offset, received.size() - offset);
                if (n > 0) {
                    offset += static_cast<size_t>(n);
                } else if (n == 0) {
                    if (offset != 0) {
                        throw std::runtime_error(
                            "TTSim Ethernet peer disconnected during handshake: " + link->read_name_);
                    }
                    // Our outgoing writer can connect before the peer opens
                    // its outgoing writer. Initial EOF is not yet peer loss.
                    wait_until(deadline, "waiting for peer handshake " + link->read_name_);
                } else if (errno != EAGAIN && errno != EINTR) {
                    fail("read peer handshake " + link->read_name_);
                } else {
                    wait_until(deadline, "waiting for peer handshake " + link->read_name_);
                }
            }
            if (received != ready) {
                throw std::runtime_error("TTSim Ethernet incompatible peer handshake: " + link->read_name_);
            }
        }
    }

private:
    [[noreturn]] static void fail(const std::string& action) {
        const int saved_errno = errno;
        throw std::runtime_error("TTSim Ethernet " + action + ": " + std::strerror(saved_errno));
    }

    static void validate_fifo(int fd) {
        struct stat st {};

        if (::fstat(fd, &st) != 0 || !S_ISFIFO(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 0777) != 0600 ||
            st.st_nlink != 1) {
            throw std::runtime_error("TTSim Ethernet endpoint must be a private FIFO owned by this user");
        }
    }

    static void wait_until(Clock::time_point deadline, const std::string& action) {
        if (Clock::now() >= deadline) {
            throw std::runtime_error("TTSim Ethernet setup deadline exceeded: " + action);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void send_ready(const char* data, size_t size) {
        sigset_t mask;
        sigset_t old_mask;
        sigset_t pending;
        ::sigemptyset(&mask);
        ::sigaddset(&mask, SIGPIPE);
        const int error = ::pthread_sigmask(SIG_BLOCK, &mask, &old_mask);
        if (error != 0) {
            throw std::runtime_error("TTSim Ethernet could not mask SIGPIPE during handshake");
        }
        ::sigpending(&pending);
        const bool already_pending = ::sigismember(&pending, SIGPIPE) == 1;
        ssize_t n;
        do {
            n = ::write(write_fd_, data, size);
        } while (n < 0 && errno == EINTR);
        const int saved_errno = errno;
        if (n < 0 && saved_errno == EPIPE && !already_pending) {
            const timespec timeout{0, 0};
            while (::sigtimedwait(&mask, nullptr, &timeout) < 0 && errno == EINTR) {
            }
        }
        ::pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        if (n != static_cast<ssize_t>(size)) {
            errno = n < 0 ? saved_errno : EIO;
            fail("write peer handshake " + write_name_);
        }
    }

    void cleanup() noexcept {
        if (write_fd_ >= 0) {
            ::close(write_fd_);
        }
        if (read_fd_ >= 0) {
            ::close(read_fd_);
        }
        if (created_) {
            ::unlinkat(dir_fd_, read_name_.c_str(), 0);
        }
        if (dir_fd_ >= 0) {
            ::close(dir_fd_);
        }
    }

    int dir_fd_ = -1;
    int read_fd_ = -1;
    int write_fd_ = -1;
    bool created_ = false;
    std::string read_name_;
    std::string write_name_;
};

}  // namespace tt::umd
