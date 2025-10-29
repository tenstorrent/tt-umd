/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sys/socket.h>
#include <sys/un.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace tt::umd {

enum class ConnectionState { DISCONNECTED, CONNECTING, CONNECTED };

class EthConnection {
public:
    EthConnection() = default;
    ~EthConnection();
    void create_socket(const std::string& address, bool abstract_socket, bool is_server);
    bool connect();
    void disconnect();
    bool is_connected() const;
    // Returns the write_fd and read_fd
    std::pair<int, int> get_fds() const;

private:
    int client_fd_ = -1;
    int server_fd_ = -1;
    ConnectionState state_ = ConnectionState::DISCONNECTED;
    bool is_server_ = false;
    struct sockaddr_un addr_;
    socklen_t addr_len_ = 0;
    static constexpr uint32_t default_buffer_size_ = 5 * 1024;
};

}  // namespace tt::umd
