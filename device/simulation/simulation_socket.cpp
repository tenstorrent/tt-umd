// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_socket.hpp"

#include <fmt/format.h>
#include <sys/un.h>  // sockaddr_un::sun_path, for the path-length guard

#include <asio.hpp>
#include <memory>
#include <system_error>
#include <thread>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

using stream_protocol = asio::local::stream_protocol;

// Builds an endpoint for a pathname socket, throwing if the path is too long for sockaddr_un.
// asio's endpoint(path) would itself throw on overflow, but with a less actionable message.
stream_protocol::endpoint make_endpoint(const std::filesystem::path& path) {
    if (path.string().size() >= sizeof(sockaddr_un::sun_path)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Simulation server socket path is too long ({} >= {}): {}",
                path.string().size(),
                sizeof(sockaddr_un::sun_path),
                path.string()));
    }
    return stream_protocol::endpoint(path.string());
}

}  // namespace

// The asio transport, kept out of the header (see SimulationSocket::Impl forward declaration).
struct SimulationSocket::Impl {
    asio::io_context io;
    stream_protocol::acceptor acceptor{io};
    std::thread io_thread;
};

SimulationSocket::SimulationSocket(const std::filesystem::path& socket_path) :
    socket_path_(socket_path), impl_(std::make_unique<Impl>()) {}

std::unique_ptr<SimulationSocket> SimulationSocket::try_create(const std::filesystem::path& socket_path) {
    std::unique_ptr<SimulationSocket> socket(new SimulationSocket(socket_path));
    if (!socket->bind_and_listen()) {
        return nullptr;
    }
    return socket;
}

std::unique_ptr<SimulationSocket> SimulationSocket::create(const std::filesystem::path& socket_path) {
    auto socket = try_create(socket_path);
    if (!socket) {
        UMD_THROW(
            error::RuntimeError, fmt::format("A live simulation server already exists at: {}", socket_path.string()));
    }
    return socket;
}

void SimulationSocket::do_accept() {
    auto sock = std::make_shared<stream_protocol::socket>(impl_->io);
    impl_->acceptor.async_accept(*sock, [this, sock](const std::error_code& ec) {
        // A non-aborted error or io_context::stop() (teardown) ends the loop; otherwise
        // drop this connection (liveness only) and re-arm.
        if (ec) {
            return;
        }
        do_accept();
    });
}

bool SimulationSocket::is_live() {
    // is_live() is a predicate; a path too long to name a reachable listener can't have one,
    // so report not-live rather than letting make_endpoint() throw.
    if (socket_path_.string().size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
    }
    // Reuse impl_->io rather than spin up a throwaway io_context: a synchronous connect()
    // doesn't run the event loop, so the shared context is untouched (cf. warm_reset.cpp).
    stream_protocol::socket probe(impl_->io);
    std::error_code ec;
    probe.connect(make_endpoint(socket_path_), ec);
    return !ec;
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

    const stream_protocol::endpoint endpoint = make_endpoint(socket_path_);

    // The acceptor closes its fd on destruction (RAII), so error paths only need to undo the
    // socket *file* that bind() created -- the fd is handled when this object is torn down.
    std::error_code ec;
    impl_->acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to create simulation server socket: {}", ec.message()));
    }

    impl_->acceptor.bind(endpoint, ec);
    // Compare against asio's own error constant: standalone asio uses a system category that
    // does not bridge to std::generic_category, so `ec == std::errc::address_in_use` is false.
    if (ec == asio::error::address_in_use) {
        // Something already holds the path. A live owner means we must attach as a client
        // instead (return false); a stale leftover (crashed owner) is reclaimed.
        //
        // Reclaim is best-effort and assumes a single contender per path: the
        // is_live() -> remove() -> re-bind() sequence is a TOCTOU window, so two
        // processes racing to reclaim the same stale socket (or a new owner binding in
        // the gap) is undefined. The socket dir is assumed trusted (see
        // default_socket_path), which is what makes this acceptable. Note a stale socket
        // owned by another user may not be removable under a sticky dir (e.g. /tmp).
        if (is_live()) {
            return false;
        }
        // Reclaim only an actual stale socket. A non-socket object squatting the path (regular
        // file, dir) also yields EADDRINUSE on bind; refuse to unlink it rather than destroy
        // unrelated data, even though the dir is assumed trusted. remove() errors are surfaced
        // (not ignored) so a failed reclaim doesn't fall through to a confusing re-bind failure.
        std::error_code stat_ec;
        if (!std::filesystem::is_socket(socket_path_, stat_ec)) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Cannot host simulation server: {} is in use by a non-socket file; refusing to remove it",
                    socket_path_.string()));
        }
        std::error_code remove_ec;
        std::filesystem::remove(socket_path_, remove_ec);
        if (remove_ec) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Failed to reclaim stale simulation server socket at {}: {}",
                    socket_path_.string(),
                    remove_ec.message()));
        }
        impl_->acceptor.bind(endpoint, ec);
        if (ec) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Failed to bind simulation server socket at {} after reclaim: {}",
                    socket_path_.string(),
                    ec.message()));
        }
    } else if (ec) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to bind simulation server socket at {}: {}", socket_path_.string(), ec.message()));
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
        std::filesystem::remove(socket_path_);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to set permissions on simulation server socket at {}: {}",
                socket_path_.string(),
                perm_ec.message()));
    }

    impl_->acceptor.listen(/*backlog=*/16, ec);
    if (ec) {
        std::filesystem::remove(socket_path_);
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to listen on simulation server socket at {}: {}", socket_path_.string(), ec.message()));
    }

    do_accept();
    impl_->io_thread = std::thread([this] { impl_->io.run(); });
    bound_ = true;
    return true;
}

SimulationSocket::~SimulationSocket() {
    // Only a successfully-bound owner tears down and removes the file. A never-bound object
    // (try_create() lost to a live owner and returned nullptr) must not delete the live
    // owner's socket. The throwing ctor is safe either way: if bind_and_listen() throws,
    // bound_ is still false, so this destructor is a no-op for the file.
    if (!bound_) {
        return;
    }
    // Wake the accept loop and let impl_->io.run() return; the acceptor fd is closed by the
    // acceptor's destructor (RAII) when impl_ is destroyed.
    impl_->io.stop();
    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }
    // Non-throwing overload: the destructor is implicitly noexcept, so a thrown
    // filesystem_error would call std::terminate. Cleanup is best-effort.
    std::error_code ec;
    std::filesystem::remove(socket_path_, ec);
}

std::filesystem::path SimulationSocket::default_socket_path(ChipId chip_id) {
    // One shared socket per chip per machine, under the system temp directory: the name
    // carries no uid, so every process (any user) resolves the same path and attaches to the
    // single host. The socket dir is assumed trusted: the path is predictable and the socket
    // is world-writable (see bind_and_listen), so any local user can connect to or squat it.
    return std::filesystem::temp_directory_path() / fmt::format("tt-umd-sim-{}.sock", chip_id);
}

}  // namespace tt::umd
