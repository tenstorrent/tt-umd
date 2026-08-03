// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Shared helpers for the two-host dma-buf RDMA smoke test. Not part of tt-umd; standalone scratch
// tooling to validate Cluster::export_dmabuf() over real RDMA hardware.
#pragma once

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
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

// Format a GID for logging, matching the ipv4-mapped-aware style of `show_gids`.
inline std::string gid_to_string(const ibv_gid& gid) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET6, gid.raw, buf, sizeof(buf))) {
        return "<unprintable>";
    }
    return buf;
}

// Pick the GID index to use for a RoCE port.
//
// This matters a lot and is easy to get wrong: on these Broadcom BCM957608 NICs, GID index 0 is
// RoCE *v1* with a link-local fe80:: address. RoCEv1 is a non-routable L2 protocol, and connecting
// with it (even between hosts on the same subnet) produces no traffic the peer ever ACKs — the
// symptom is the initiator failing its very first WRITE with "transport retry counter exceeded",
// which is indistinguishable at a glance from a hardware/dma-buf problem. `ib_write_bw -x 0`
// reproduces the same stall, so it's a fabric-config issue, not anything to do with UMD.
//
// Prefer a RoCEv2 GID with an IPv4-mapped address (::ffff:a.b.c.d), which is the routable,
// switch-friendly choice; fall back to any non-link-local RoCEv2 GID. Returns -1 if none is found.
inline int find_roce_v2_gid_index(ibv_context* ctx, int ib_port) {
    ibv_port_attr port_attr{};
    if (ibv_query_port(ctx, ib_port, &port_attr)) {
        return -1;
    }

    int fallback = -1;
    for (int i = 0; i < port_attr.gid_tbl_len; ++i) {
        ibv_gid_entry entry{};
        if (ibv_query_gid_ex(ctx, static_cast<uint32_t>(ib_port), static_cast<uint32_t>(i), &entry, 0)) {
            continue;  // ENODATA for empty table slots
        }
        if (entry.gid_type != IBV_GID_TYPE_ROCE_V2) {
            continue;
        }

        const uint8_t* r = entry.gid.raw;
        bool ipv4_mapped = (r[10] == 0xff && r[11] == 0xff);
        for (int b = 0; b < 10 && ipv4_mapped; ++b) {
            ipv4_mapped = (r[b] == 0x00);
        }
        if (ipv4_mapped) {
            return i;
        }
        bool link_local = (r[0] == 0xfe && r[1] == 0x80);
        if (!link_local && fallback < 0) {
            fallback = i;
        }
    }
    return fallback;
}

// Resolve the GID index to use: honor an explicit override if given (>= 0), otherwise auto-detect.
// Throws if neither yields a usable RoCEv2 GID, since silently falling back to index 0 is exactly
// the failure mode described above.
inline int resolve_gid_index(ibv_context* ctx, int ib_port, int requested) {
    if (requested >= 0) {
        ibv_gid_entry entry{};
        if (!ibv_query_gid_ex(ctx, static_cast<uint32_t>(ib_port), static_cast<uint32_t>(requested), &entry, 0) &&
            entry.gid_type != IBV_GID_TYPE_ROCE_V2) {
            std::cerr << "Warning: --gid-index " << requested << " is not a RoCEv2 GID; "
                      << "expect 'transport retry counter exceeded' if the fabric is routed." << std::endl;
        }
        return requested;
    }

    int idx = find_roce_v2_gid_index(ctx, ib_port);
    if (idx < 0) {
        throw std::runtime_error(
            "No RoCEv2 GID found on this port; pass --gid-index explicitly (see the GID table under "
            "/sys/class/infiniband/<dev>/ports/<port>/gid_attrs/types/)");
    }

    ibv_gid gid{};
    ibv_query_gid(ctx, ib_port, idx, &gid);
    std::cout << "Auto-selected RoCEv2 GID index " << idx << " (" << gid_to_string(gid) << ")" << std::endl;
    return idx;
}

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
