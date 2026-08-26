// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_client.hpp"

#include <fmt/format.h>
#include <poll.h>    // poll(), to bound the wait on the socket
#include <sys/un.h>  // sockaddr_un::sun_path, for the path-length guard

#include <asio.hpp>
#include <cerrno>
#include <chrono>
#include <system_error>

#include "simulation/simulation_server_transport.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

using stream_protocol = asio::local::stream_protocol;

// Builds an endpoint for a pathname socket, throwing if the path is too long for sockaddr_un.
// asio's endpoint(path) would itself throw on overflow, but with a less actionable message.
// Named distinctly from SimulationServerSocket's equivalent helper: both files are always built
// and share a unity-build translation unit, where two anonymous-namespace make_endpoint()s
// would collide.
stream_protocol::endpoint make_client_endpoint(const std::filesystem::path& path) {
    if (path.string().size() >= sizeof(sockaddr_un::sun_path)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Simulation host socket path is too long ({} >= {}): {}",
                path.string().size(),
                sizeof(sockaddr_un::sun_path),
                path.string()));
    }
    return stream_protocol::endpoint(path.string());
}

// Wait up to `timeout` for the socket to be ready for `events` (POLLIN/POLLOUT), throwing on
// timeout or error. This bounds the exchange: the transport does blocking reads/writes, and asio's
// synchronous read masks SO_RCVTIMEO (on EAGAIN it polls untimed and retries), so an explicit
// poll() is what actually stops transact() from hanging forever on a host that accepted the
// connection (the listen backlog does that even before it serves) but never answers.
void wait_for_socket(
    int fd, short events, std::chrono::milliseconds timeout, const std::filesystem::path& path, const char* what) {
    struct pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = events;
    int rc = 0;
    do {
        rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    } while (rc < 0 && errno == EINTR);
    UMD_ASSERT(
        rc >= 0, error::RuntimeError, fmt::format("poll() on simulation host socket at {} failed", path.string()));
    UMD_ASSERT(
        rc != 0,
        error::RuntimeError,
        fmt::format(
            "Timed out after {} ms waiting to {} on simulation host at {} (host reachable but not responding)",
            timeout.count(),
            what,
            path.string()));
    // poll() reports the fd "ready" for reasons other than `events` too. A hard error (POLLERR/
    // POLLNVAL) means the send/recv that follows can only fail, so surface it here with the poll
    // state rather than let it come back as a murkier transport error. POLLHUP alone (peer closed,
    // no data) is likewise not readiness; but POLLHUP *with* POLLIN is -- there may be buffered
    // bytes to read before EOF -- so we gate on `events` being set rather than rejecting POLLHUP.
    if (pfd.revents & (POLLERR | POLLNVAL)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Socket error while waiting to {} on simulation host at {} (poll revents=0x{:x})",
                what,
                path.string(),
                static_cast<unsigned>(pfd.revents)));
    }
    if (!(pfd.revents & events)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Simulation host at {} closed the connection before we could {} (poll revents=0x{:x})",
                path.string(),
                what,
                static_cast<unsigned>(pfd.revents)));
    }
}

}  // namespace

// The asio transport, kept out of the header (see SimulationClient::Impl forward declaration).
struct SimulationClient::Impl {
    asio::io_context io;
    stream_protocol::socket socket{io};
};

SimulationClient::SimulationClient(std::filesystem::path socket_path, std::chrono::milliseconds timeout) :
    socket_path_(std::move(socket_path)), timeout_(timeout), impl_(std::make_unique<Impl>()) {}

SimulationClient::~SimulationClient() { detach(); }

void SimulationClient::attach() {
    if (impl_->socket.is_open()) {
        return;  // Already attached.
    }

    std::error_code ec;
    impl_->socket.connect(make_client_endpoint(socket_path_), ec);
    if (ec) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to connect to simulation host socket at {}: {}", socket_path_.string(), ec.message()));
    }
    // Ensure blocking mode (the transport uses blocking reads/writes); if that fails, fail loudly
    // rather than report a half-attached client as connected.
    impl_->socket.native_non_blocking(false, ec);
    if (ec) {
        std::error_code close_ec;
        impl_->socket.close(close_ec);  // drop the half-open socket; is_open() -> false, so not "attached"
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to set blocking mode on simulation host socket at {}: {}",
                socket_path_.string(),
                ec.message()));
    }
}

void SimulationClient::detach() {
    if (!impl_->socket.is_open()) {
        return;
    }
    // ec suppresses exceptions so close() still runs even if shutdown() fails (e.g. the peer
    // already dropped the connection); the file descriptor release is what must be guaranteed.
    std::error_code ec;
    impl_->socket.shutdown(stream_protocol::socket::shutdown_both, ec);
    impl_->socket.close(ec);
}

std::vector<uint8_t> SimulationClient::transact(const std::vector<uint8_t>& request) {
    if (!impl_->socket.is_open()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Cannot transact with simulation host at {}: not attached", socket_path_.string()));
    }
    const int fd = impl_->socket.native_handle();
    wait_for_socket(fd, POLLOUT, timeout_, socket_path_, "send the request");
    send_framed(impl_->socket, request);
    // Bound the wait for the host to start replying -- the hang this guards against is a host that
    // accepted the connection but never answers.
    wait_for_socket(fd, POLLIN, timeout_, socket_path_, "receive the reply");
    return recv_framed(impl_->socket);
}

}  // namespace tt::umd
