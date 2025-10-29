/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eth_connection.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>

#include "assert.hpp"

namespace tt::umd {

EthConnection::~EthConnection() { disconnect(); }

void EthConnection::create_socket(const std::string& address, bool abstract_socket, bool is_server) {
    if (state_ != ConnectionState::DISCONNECTED) {
        TT_THROW("EthConnection already configured");
    }
    if (address.empty()) {
        TT_THROW("Address is empty");
    } else if (address.length() > sizeof(addr_.sun_path) - 1) {
        TT_THROW("Address is too long");
    }
    // Setup Unix socket address
    memset(&addr_, 0, sizeof(addr_));
    addr_.sun_family = AF_UNIX;
    if (abstract_socket) {
        addr_.sun_path[0] = '\0';
        strncpy(addr_.sun_path + 1, address.data(), address.length());
        // Calculate address length for abstract sockets: offsetof + 1 (for leading '\0') + name length
        addr_len_ = offsetof(struct sockaddr_un, sun_path) + 1 + address.length();
    } else {
        strncpy(addr_.sun_path, address.data(), address.length() + 1);
        addr_len_ = sizeof(addr_);
    }

    // Create Unix domain socket for listening
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        TT_THROW("Socket failed to create: {}", strerror(errno));
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        TT_THROW("Socket failed to set to non-blocking: {}", strerror(errno));
    }
    is_server_ = is_server;

    if (!is_server) {
        client_fd_ = fd;
        // Configure socket buffer sizes for client socket
        if (setsockopt(client_fd_, SOL_SOCKET, SO_SNDBUF, &default_buffer_size_, sizeof(default_buffer_size_)) < 0) {
            TT_THROW("Failed to set send buffer size for client socket: {}", strerror(errno));
        }
        if (setsockopt(client_fd_, SOL_SOCKET, SO_RCVBUF, &default_buffer_size_, sizeof(default_buffer_size_)) < 0) {
            TT_THROW("Failed to set receive buffer size for client socket: {}", strerror(errno));
        }
        return;
    }

    if (!abstract_socket) {
        // Unlink the socket file if it already exists
        unlink(addr_.sun_path);
    }

    // Bind socket to address
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr_), addr_len_) < 0) {
        TT_THROW("Server socket failed to bind socket: {}", strerror(errno));
    }

    // Start listening for connections (backlog of 1 for single connection)
    if (listen(fd, 1) < 0) {
        TT_THROW("Server socket failed to listen on socket: {}", strerror(errno));
    }
    server_fd_ = fd;
}

bool EthConnection::connect() {
    if (state_ == ConnectionState::CONNECTED) {
        return true;
    }

    if (!is_server_) {
        if (client_fd_ == -1) {
            return false;
        }
        if (state_ == ConnectionState::CONNECTING) {
            // Use poll to wait for connection completion
            struct pollfd pfd;
            pfd.fd = client_fd_;
            pfd.events = POLLOUT;
            pfd.revents = 0;

            int poll_result = poll(&pfd, 1, 0);

            if (poll_result < 0) {
                TT_THROW("Client socket poll failed: {}", strerror(errno));
            } else if (poll_result == 0) {
                return false;
            }

            // Check if connection was successful
            int error = 0;
            socklen_t len = sizeof(error);

            if (getsockopt(client_fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                TT_THROW("Client socket failed to get socket error: {}", strerror(errno));
            }

            if (error != 0) {
                TT_THROW("Client socket connection failed: {}", strerror(error));
            }
            state_ = ConnectionState::CONNECTED;
            return true;
        }
        int result = ::connect(client_fd_, reinterpret_cast<struct sockaddr*>(&addr_), addr_len_);
        if (result < 0) {
            if (errno == EINPROGRESS) {
                state_ = ConnectionState::CONNECTING;
                return false;
            } else if (errno == ECONNREFUSED) {
                return false;
            } else {
                TT_THROW("Client socket failed to initiate socket connection: {}", strerror(errno));
            }
        } else {
            state_ = ConnectionState::CONNECTED;
            return true;
        }
    } else {
        if (server_fd_ == -1) {
            TT_THROW("Server socket not created");
        }
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        client_fd_ = accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_fd_ == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No connection available, wait and retry
                return false;
            } else {
                TT_THROW("Server socket failed to accept socket connection: {}", strerror(errno));
            }
        }
        // Configure socket buffer sizes for accepted client socket
        if (setsockopt(client_fd_, SOL_SOCKET, SO_SNDBUF, &default_buffer_size_, sizeof(default_buffer_size_)) < 0) {
            TT_THROW("Failed to set send buffer size for accepted socket: {}", strerror(errno));
        }
        if (setsockopt(client_fd_, SOL_SOCKET, SO_RCVBUF, &default_buffer_size_, sizeof(default_buffer_size_)) < 0) {
            TT_THROW("Failed to set receive buffer size for accepted socket: {}", strerror(errno));
        }
        state_ = ConnectionState::CONNECTED;
        return true;
    }
    return false;
}

void EthConnection::disconnect() {
    if (client_fd_ != -1) {
        close(client_fd_);
        client_fd_ = -1;
    }
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    state_ = ConnectionState::DISCONNECTED;
}

bool EthConnection::is_connected() const { return state_ == ConnectionState::CONNECTED; }

std::pair<int, int> EthConnection::get_fds() const { return std::make_pair(client_fd_, client_fd_); }

}  // namespace tt::umd
