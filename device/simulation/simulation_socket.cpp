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
#include <optional>
#include <system_error>
#include <utility>

#include "umd/device/simulation/simulation_server_api.hpp"
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
    // is_live() is a predicate; a path too long for sockaddr_un can't name a reachable
    // listener, so report not-live rather than throwing (make_unix_addr would UMD_THROW).
    if (path.string().size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
    }
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
        // A socket is only reachable cross-user if the directory holding it is too, so a
        // directory we create for it gets /tmp's semantics: 0777 + sticky bit. World rwx lets
        // any user host (bind creates the file) or attach (traverse to connect); the sticky
        // bit still stops users from deleting each other's sockets. We only adjust a directory
        // we created here -- a pre-existing dir (e.g. the system temp dir) is left untouched.
        if (std::filesystem::create_directories(socket_path_.parent_path())) {
            std::error_code dir_perm_ec;
            std::filesystem::permissions(
                socket_path_.parent_path(),
                std::filesystem::perms::all | std::filesystem::perms::sticky_bit,
                std::filesystem::perm_options::replace,
                dir_perm_ec);
        }
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
            //
            // Reclaim is best-effort and assumes a single contender per path: the
            // is_live() -> remove() -> re-bind() sequence is a TOCTOU window, so two
            // processes racing to reclaim the same stale socket (or a new owner binding in
            // the gap) is undefined. The socket dir is assumed trusted (see
            // default_socket_path), which is what makes this acceptable. Note a stale socket
            // owned by another user may not be removable under a sticky dir (e.g. /tmp).
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

    // The socket must be connectable by any user (cross-user attach): bind() created it with
    // only umask-dependent permissions, and connect() to a UNIX socket requires write
    // permission on the file. Make it world-readable/writable so any local user can attach.
    std::error_code perm_ec;
    std::filesystem::permissions(
        socket_path_,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read |
            std::filesystem::perms::group_write | std::filesystem::perms::others_read |
            std::filesystem::perms::others_write,
        std::filesystem::perm_options::replace,
        perm_ec);
    if (perm_ec) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::filesystem::remove(socket_path_);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to set permissions on simulation server socket at {}: {}",
                socket_path_.string(),
                perm_ec.message()));
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
    bound_ = true;
    return true;
}

void SimulationSocket::start_serving(RequestHandler handler) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    handler_ = std::move(handler);
}

SimulationSocket::~SimulationSocket() {
    // Only a successfully-bound owner tears down and removes the file. A never-bound object
    // (try_create() lost to a live owner and returned nullptr) must not delete the live
    // owner's socket. The throwing ctor is unaffected: a throw from its body skips the
    // destructor entirely.
    if (!bound_) {
        return;
    }
    running_ = false;
    if (shutdown_pipe_[1] >= 0) {
        const char byte = 0;
        // Best-effort wake of the accept loop; the running_ flag is the source of truth.
        [[maybe_unused]] ssize_t written = ::write(shutdown_pipe_[1], &byte, 1);
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Unblock any worker stuck in a read, then join them. Workers close their own fd on exit
    // (under the mutex), so we only shut down fds still marked open. The lock is released before
    // joining so workers can take it to close.
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (int fd : connection_fds_) {
            if (fd >= 0) {
                ::shutdown(fd, SHUT_RDWR);
            }
        }
    }
    for (std::thread& worker : connection_threads_) {
        if (worker.joinable()) {
            worker.join();
        }
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
    // Non-throwing overload: the destructor is implicitly noexcept, so a thrown
    // filesystem_error would call std::terminate. Cleanup is best-effort.
    std::error_code ec;
    std::filesystem::remove(socket_path_, ec);
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
            if (client_fd < 0) {
                continue;
            }
            std::lock_guard<std::mutex> lock(connections_mutex_);
            if (!handler_) {
                // Not serving yet (owner-only / pre-start_serving): accept and drop.
                ::close(client_fd);
                continue;
            }
            const size_t index = connection_fds_.size();
            connection_fds_.push_back(client_fd);
            connection_threads_.emplace_back([this, client_fd, index] { serve_connection(client_fd, index); });
        }
    }
}

void SimulationSocket::serve_connection(int client_fd, size_t index) {
    while (running_) {
        std::optional<std::vector<uint8_t>> request = recv_framed(client_fd);
        if (!request) {
            break;  // Client disconnected or error.
        }
        const std::vector<uint8_t> response = handler_(*request);
        if (!send_framed(client_fd, response)) {
            break;
        }
    }
    std::lock_guard<std::mutex> lock(connections_mutex_);
    ::close(client_fd);
    connection_fds_[index] = -1;
}

std::filesystem::path SimulationSocket::default_socket_path(ChipId chip_id) {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    if (const char* env = std::getenv("TT_UMD_SIM_SOCKET_DIR")) {
        dir = std::filesystem::path(env);
    }
    // One shared socket per chip per machine: the name carries no uid, so every process
    // (any user) resolves the same path and attaches to the single host. The socket dir is
    // assumed trusted: the path is predictable and the socket is world-writable (see
    // bind_and_listen), so any local user can connect to or squat it. $TT_UMD_SIM_SOCKET_DIR
    // is taken verbatim.
    return dir / fmt::format("tt-umd-sim-{}.sock", chip_id);
}

}  // namespace tt::umd
