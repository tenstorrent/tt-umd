// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_server_socket.hpp"

#include <fmt/format.h>
#include <sys/un.h>  // sockaddr_un::sun_path, for the path-length guard

#include <asio.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "simulation/simulation_server_transport.hpp"
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

// The asio transport, kept out of the header (see SimulationServerSocket::Impl forward declaration).
struct SimulationServerSocket::Impl {
    asio::io_context io;
    stream_protocol::acceptor acceptor{io};
    std::thread io_thread;

    // Per-connection serving state, held only while a handler is set. Finished connections (peer
    // disconnected, flagged by the serving thread) are reaped so this doesn't grow unbounded across
    // connect/disconnect cycles. Guarded: registered/reaped on the io_thread, torn down on the
    // destructor thread.
    struct Connection {
        std::shared_ptr<stream_protocol::socket> socket;
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    std::mutex connections_mutex;
    std::vector<Connection> connections;
    bool stopping = false;
};

SimulationServerSocket::SimulationServerSocket(const std::filesystem::path& socket_path) :
    socket_path_(socket_path), impl_(std::make_unique<Impl>()) {}

std::unique_ptr<SimulationServerSocket> SimulationServerSocket::try_create(const std::filesystem::path& socket_path) {
    std::unique_ptr<SimulationServerSocket> socket(new SimulationServerSocket(socket_path));
    if (!socket->bind_and_listen()) {
        return nullptr;
    }
    // Bound and connectable now (presence/liveness); serving begins when serve() is called with a
    // handler bound to the owner's backend.
    return socket;
}

std::unique_ptr<SimulationServerSocket> SimulationServerSocket::create(const std::filesystem::path& socket_path) {
    auto socket = try_create(socket_path);
    if (!socket) {
        UMD_THROW(
            error::RuntimeError, fmt::format("A live simulation server already exists at: {}", socket_path.string()));
    }
    return socket;
}

void SimulationServerSocket::serve(RequestHandler request_handler) {
    UMD_ASSERT(bound_, error::RuntimeError, "serve() called on a simulation server socket that is not bound.");
    UMD_ASSERT(
        static_cast<bool>(request_handler), error::RuntimeError, "serve() requires a non-empty request handler.");
    UMD_ASSERT(
        !impl_->io_thread.joinable(),
        error::RuntimeError,
        "serve() called on a simulation server socket already serving.");

    // Install the handler before the accept loop starts, so it is set before any connection
    // thread reads it -- no mutation of request_handler_ once the io_thread is running.
    request_handler_ = std::move(request_handler);
    do_accept();
    impl_->io_thread = std::thread([this] { impl_->io.run(); });
}

void SimulationServerSocket::do_accept() {
    auto sock = std::make_shared<stream_protocol::socket>(impl_->io);
    impl_->acceptor.async_accept(*sock, [this, sock](const std::error_code& ec) {
        // A non-aborted error or io_context::stop() (teardown) ends the loop.
        if (ec) {
            return;
        }

        // Serve the connection on its own thread until the peer disconnects. do_accept only runs
        // while serving, so request_handler_ is always set here.
        // asio leaves accepted sockets non-blocking (for async I/O); the serving thread does
        // synchronous, blocking transport reads/writes on it, so clear that flag first. If that
        // fails, the blocking reads/writes would error immediately -- drop this connection cleanly
        // and keep accepting rather than spawn a thread that can't serve.
        std::error_code block_ec;
        sock->native_non_blocking(false, block_ec);
        if (block_ec) {
            do_accept();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(impl_->connections_mutex);
            // A connection accepted during teardown is simply dropped (sock released here).
            if (!impl_->stopping) {
                // Reap connections whose serving thread has already exited (peer disconnected), so
                // the bookkeeping doesn't grow without bound across connect/disconnect cycles.
                for (auto it = impl_->connections.begin(); it != impl_->connections.end();) {
                    if (it->finished->load()) {
                        if (it->thread.joinable()) {
                            it->thread.join();
                        }
                        it = impl_->connections.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Capture the handler by copy so the thread never touches `this`; keep `sock`
                // alive so the socket stays open for the whole session; set `finished` on exit so
                // this connection can be reaped above.
                auto finished = std::make_shared<std::atomic<bool>>(false);
                std::thread thread([handler = request_handler_, sock, finished] {
                    try {
                        while (true) {
                            send_framed(*sock, handler(recv_framed(*sock)));
                        }
                    } catch (const std::exception&) {
                        // Peer disconnected (recv_framed threw on EOF) or a socket error: the
                        // session ends. Teardown joins this thread; the socket closes with `sock`.
                    }
                    finished->store(true);
                });
                impl_->connections.push_back({sock, std::move(thread), finished});
            }
        }

        do_accept();
    });
}

bool SimulationServerSocket::is_live() {
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

bool SimulationServerSocket::bind_and_listen() {
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

    // Bound and connectable (liveness) now; the accept loop and io_thread start in serve().
    bound_ = true;
    return true;
}

SimulationServerSocket::~SimulationServerSocket() {
    // Only a successfully-bound owner tears down and removes the file. A never-bound object
    // (try_create() lost to a live owner and returned nullptr) must not delete the live
    // owner's socket. The throwing ctor is safe either way: if bind_and_listen() throws,
    // bound_ is still false, so this destructor is a no-op for the file.
    if (!bound_) {
        return;
    }

    // Stop serving: mark stopping (so a racing accept won't start a new session) and shut down
    // every live connection so its serving thread, blocked in recv_framed, sees EOF and exits.
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mutex);
        impl_->stopping = true;
        for (const auto& connection : impl_->connections) {
            std::error_code ec;
            connection.socket->shutdown(stream_protocol::socket::shutdown_both, ec);
        }
    }
    for (auto& connection : impl_->connections) {
        if (connection.thread.joinable()) {
            connection.thread.join();
        }
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

std::filesystem::path SimulationServerSocket::default_socket_path(ChipId chip_id) {
    // One shared socket per chip per machine, under the system temp directory: the name
    // carries no uid, so every process (any user) resolves the same path and attaches to the
    // single host. The socket dir is assumed trusted: the path is predictable and the socket
    // is world-writable (see bind_and_listen), so any local user can connect to or squat it.
    return std::filesystem::temp_directory_path() / fmt::format("tt-umd-sim-{}.sock", chip_id);
}

std::optional<ChipId> SimulationServerSocket::chip_id_from_socket_path(const std::filesystem::path& socket_path) {
    // Inverse of default_socket_path()'s "tt-umd-sim-<chip_id>.sock". Kept next to it so the naming
    // convention lives in exactly one place.
    static constexpr std::string_view prefix = "tt-umd-sim-";
    static constexpr std::string_view suffix = ".sock";
    const std::string name = socket_path.filename().string();
    if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return std::nullopt;
    }
    const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    // Require a plain run of digits so e.g. "tt-umd-sim-.sock" or "tt-umd-sim-x.sock" don't parse.
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    try {
        return static_cast<ChipId>(std::stoi(digits));
    } catch (const std::exception&) {
        return std::nullopt;  // out of int range
    }
}

std::map<ChipId, std::filesystem::path> SimulationServerSocket::sockets_in_directory(
    const std::filesystem::path& directory) {
    std::map<ChipId, std::filesystem::path> sockets;
    // directory_iterator(dir, ec) sets ec if the path is not a directory or cannot be read; in
    // either case there are no per-chip sockets to report (the caller then classifies it as a host
    // build, not a client socket directory), so return empty rather than silently proceeding.
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) {
        return sockets;
    }
    for (const auto& entry : it) {
        std::error_code sock_ec;
        if (!entry.is_socket(sock_ec)) {
            continue;
        }
        if (const std::optional<ChipId> chip_id = chip_id_from_socket_path(entry.path())) {
            sockets.emplace(*chip_id, entry.path());
        }
    }
    return sockets;
}

}  // namespace tt::umd
