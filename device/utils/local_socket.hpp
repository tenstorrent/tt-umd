// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <asio.hpp>
#include <functional>
#include <system_error>
#include <utility>
#ifdef __APPLE__
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace tt::umd {

// The acceptor and socket must outlive the completion handler, as with
// asio::async_accept. Only one accept may be outstanding on this acceptor.
inline void async_accept_local(
    asio::local::stream_protocol::acceptor& acceptor,
    asio::local::stream_protocol::socket& socket,
    std::function<void(std::error_code)> handler) {
#ifdef __APPLE__
    // Asio discards accepted Darwin sockets if SO_NOSIGPIPE fails with EINVAL.
    // A sender that already closed can still have unread data (reset
    // notifications), so retain that socket and its queued bytes instead.
    std::error_code ec;
    acceptor.native_non_blocking(true, ec);
    if (ec) {
        asio::post(acceptor.get_executor(), [handler = std::move(handler), ec] { handler(ec); });
        return;
    }
    acceptor.async_wait(
        asio::socket_base::wait_read, [&acceptor, &socket, handler = std::move(handler)](std::error_code ec) mutable {
            if (ec) {
                handler(ec);
                return;
            }
            int fd;
            do {
                fd = ::accept(acceptor.native_handle(), nullptr, nullptr);
            } while (fd < 0 && errno == EINTR);
            if (fd < 0) {
                ec = {errno, asio::error::get_system_category()};
                if (ec == asio::error::would_block || ec == asio::error::try_again ||
                    ec == asio::error::connection_aborted) {
                    async_accept_local(acceptor, socket, std::move(handler));
                    return;
                }
                handler(ec);
                return;
            }
            const int no_sigpipe = 1;
            if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0 && errno != EINVAL) {
                ec = {errno, asio::error::get_system_category()};
            } else {
                socket.assign(asio::local::stream_protocol{}, fd, ec);
            }
            if (ec) {
                ::close(fd);
            }
            handler(ec);
        });
#else
    acceptor.async_accept(socket, std::move(handler));
#endif
}

}  // namespace tt::umd
