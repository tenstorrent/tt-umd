// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// SPDX-License-Identifier: Apache-2.0
// Shared helpers for the two-host dma-buf RDMA smoke test. Not part of tt-umd; standalone scratch
// tooling to validate Cluster::export_dmabuf() over real RDMA hardware.
#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

// Out-of-band info exchanged between the two hosts to bring up the RC QP.
// Sent as a flat, fixed-size struct over a plain TCP socket (no serialization library needed).
struct QpExchangeInfo {
    uint32_t qpn = 0;
    uint32_t psn = 0;
    uint16_t lid = 0;          // valid only if the port's link layer is InfiniBand
    uint8_t gid[16] = {0};     // valid only if the port's link layer is Ethernet (RoCE)
    uint32_t rkey = 0;         // target's dma-buf MR rkey; unused/zero from the initiator
    uint64_t remote_addr = 0;  // target's dma-buf MR iova (we register with iova=0, so this is 0);
                               // kept explicit so the initiator doesn't have to hardcode it
};

inline void send_all(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n <= 0) {
            throw std::runtime_error("send_all: connection error");
        }
        sent += static_cast<size_t>(n);
    }
}

inline void recv_all(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) {
            throw std::runtime_error("recv_all: connection error");
        }
        got += static_cast<size_t>(n);
    }
}

// Target side: listen on `port`, accept exactly one connection, return its fd.
inline int tcp_listen_accept(uint16_t port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("socket() failed");
    }
    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed");
    }
    if (listen(listen_fd, 1) < 0) {
        throw std::runtime_error("listen() failed");
    }
    int conn_fd = accept(listen_fd, nullptr, nullptr);
    if (conn_fd < 0) {
        throw std::runtime_error("accept() failed");
    }
    close(listen_fd);
    return conn_fd;
}

// Initiator side: connect to host:port, return the connected fd.
inline int tcp_connect(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        throw std::runtime_error("getaddrinfo() failed for host " + host);
    }
    int fd = -1;
    for (addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        throw std::runtime_error("connect() failed to " + host + ":" + port_str);
    }
    return fd;
}
