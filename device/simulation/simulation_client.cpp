// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_client.hpp"

#include <fmt/format.h>
#include <sys/un.h>  // sockaddr_un::sun_path, for the path-length guard

#include <asio.hpp>
#include <system_error>

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

}  // namespace

// The asio transport, kept out of the header (see SimulationClient::Impl forward declaration).
struct SimulationClient::Impl {
    asio::io_context io;
    stream_protocol::socket socket{io};
};

SimulationClient::SimulationClient(std::filesystem::path socket_path) :
    socket_path_(std::move(socket_path)), impl_(std::make_unique<Impl>()) {}

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

}  // namespace tt::umd
