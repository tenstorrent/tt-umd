// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_socket.hpp"

#include <fmt/format.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// Fills a sockaddr_un for a pathname socket, throwing if the path is too long.
sockaddr_un make_unix_addr(const std::filesystem::path& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.string().size() >= sizeof(addr.sun_path)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Simulation server socket path is too long ({} >= {}): {}",
                path.string().size(),
                sizeof(addr.sun_path),
                path.string()));
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return addr;
}

// Returns true if a listener is currently reachable at path.
bool is_live(const std::filesystem::path& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un addr = make_unix_addr(path);
    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

}  // namespace

SimulationSocket::SimulationSocket(const std::filesystem::path& socket_path, DeferBind) : socket_path_(socket_path) {}

SimulationSocket::SimulationSocket(const std::filesystem::path& socket_path) :
    SimulationSocket(socket_path, DeferBind{}) {
    if (!bind_and_listen()) {
        UMD_THROW(
            error::RuntimeError, fmt::format("A live simulation server already exists at: {}", socket_path_.string()));
    }
}

std::unique_ptr<SimulationSocket> SimulationSocket::try_create(const std::filesystem::path& socket_path) {
    std::unique_ptr<SimulationSocket> socket(new SimulationSocket(socket_path, DeferBind{}));
    if (!socket->bind_and_listen()) {
        return nullptr;
    }
    return socket;
}

bool SimulationSocket::bind_and_listen() {
    if (socket_path_.has_parent_path()) {
        std::filesystem::create_directories(socket_path_.parent_path());
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Failed to create simulation server socket: {}", std::strerror(errno)));
    }

    sockaddr_un addr = make_unix_addr(socket_path_);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (errno == EADDRINUSE) {
            // Something already holds the path. A live owner means we must attach as a client
            // instead (return false); a stale leftover (crashed owner) is reclaimed.
            if (is_live(socket_path_)) {
                ::close(listen_fd_);
                listen_fd_ = -1;
                return false;
            }
            std::filesystem::remove(socket_path_);
            if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
                int err = errno;
                ::close(listen_fd_);
                listen_fd_ = -1;
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format(
                        "Failed to bind simulation server socket at {} after reclaim: {}",
                        socket_path_.string(),
                        std::strerror(err)));
            }
        } else {
            int err = errno;
            ::close(listen_fd_);
            listen_fd_ = -1;
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Failed to bind simulation server socket at {}: {}", socket_path_.string(), std::strerror(err)));
        }
    }

    if (::listen(listen_fd_, /*backlog=*/16) != 0) {
        int err = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::filesystem::remove(socket_path_);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to listen on simulation server socket at {}: {}", socket_path_.string(), std::strerror(err)));
    }

    if (::pipe(shutdown_pipe_) != 0) {
        int err = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::filesystem::remove(socket_path_);
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to create simulation server shutdown pipe: {}", std::strerror(err)));
    }

    running_ = true;
    accept_thread_ = std::thread(&SimulationSocket::accept_loop, this);
    return true;
}

SimulationSocket::~SimulationSocket() {
    running_ = false;
    if (shutdown_pipe_[1] >= 0) {
        const char byte = 0;
        // Best-effort wake of the accept loop; the running_ flag is the source of truth.
        [[maybe_unused]] ssize_t written = ::write(shutdown_pipe_[1], &byte, 1);
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (shutdown_pipe_[0] >= 0) {
        ::close(shutdown_pipe_[0]);
        shutdown_pipe_[0] = -1;
    }
    if (shutdown_pipe_[1] >= 0) {
        ::close(shutdown_pipe_[1]);
        shutdown_pipe_[1] = -1;
    }
    std::filesystem::remove(socket_path_);
}

void SimulationSocket::accept_loop() {
    while (running_) {
        pollfd fds[2];
        fds[0] = {listen_fd_, POLLIN, 0};
        fds[1] = {shutdown_pipe_[0], POLLIN, 0};

        int ready = ::poll(fds, 2, /*timeout=*/-1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (fds[1].revents & POLLIN) {
            break;
        }
        if (fds[0].revents & POLLIN) {
            int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                // No request handling yet (owner-only): accept and drop the connection.
                ::close(client_fd);
            }
        }
    }
}

std::filesystem::path SimulationSocket::default_socket_path(ChipId chip_id) {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    if (const char* env = std::getenv("TT_UMD_SIM_SOCKET_DIR")) {
        dir = std::filesystem::path(env);
    }
    return dir / fmt::format("tt-umd-sim-{}-{}.sock", ::geteuid(), chip_id);
}

}  // namespace tt::umd
