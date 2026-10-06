// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Shared helpers for the two-host dma-buf RDMA example: command-line parsing, the out-of-band TCP
// handshake used to exchange QP/rkey info, RDMA device/GID selection, RC QP bring-up, and the
// verification pattern. Both binaries use these, so the bring-up exists once rather than twice.
#pragma once

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

// Out-of-band info exchanged between the two hosts to bring up the RC QP.
//
// Sent as a flat struct over a plain TCP socket (no serialization library needed). The field order
// is chosen so every member is naturally aligned with no implicit padding, and the static_assert
// below pins the resulting size: a silent layout change between the initiator's and the target's
// build would otherwise corrupt the handshake in a way that surfaces as an unrelated RDMA error.
// `magic` catches the mismatched-build case at runtime. Both hosts are assumed to be the same
// endianness (x86 <-> x86); this is an example, not a portable wire protocol.
struct QpExchangeInfo {
    uint32_t magic = 0;  // MAGIC below; guards against a mismatched or stale peer build
    uint32_t qpn = 0;
    uint32_t psn = 0;
    uint32_t rkey = 0;           // target's dma-buf MR rkey; unused/zero from the initiator
    uint64_t remote_addr = 0;    // target's dma-buf MR iova (we register with iova=0, so this is 0);
                                 // kept explicit so the initiator doesn't have to hardcode it
    uint64_t mr_length = 0;      // target's MR length in bytes. The two binaries are launched
                                 // independently with their own --size; the initiator compares this
                                 // against its own and refuses a mismatch, which would otherwise be
                                 // either IBV_WC_REM_ACCESS_ERR at iter 0 (initiator larger) or a
                                 // bandwidth figure for a partly-written window (initiator smaller).
    uint16_t lid = 0;            // valid only if the port's link layer is InfiniBand
    uint16_t reserved = 0;       // explicit, so the 16-byte gid below stays aligned with no padding
    uint8_t gid[16] = {0};       // valid only if the port's link layer is Ethernet (RoCE)
    uint8_t reserved2[4] = {0};  // explicit tail padding to the struct's 8-byte alignment, so the
                                 // wire size is stated here rather than chosen by the compiler

    static constexpr uint32_t MAGIC = 0x54544442;  // "TTDB"
};

static_assert(sizeof(QpExchangeInfo) == 56, "QpExchangeInfo must stay padding-free; the peer parses it byte-for-byte");

// Sent by the initiator after its RDMA batch, in place of the old unconditional 1 byte. The target
// propagates a FAIL into its own exit status: without this the initiator can print FAIL and return 1
// while the target returns 0, and a CI harness watching the target (the side that owns the device)
// records a clean pass on a data-corruption run.
enum class BatchResult : uint8_t {
    Pass = 1,
    Fail = 2,
};

// Position-dependent verification pattern: a golden-ratio multiplier per 64-bit word, so the value
// at a given offset is unique across any realistic window. A byte-periodic pattern (e.g. i & 0xFF)
// cannot detect a wrong-address bug at all, since any offset that is a multiple of the period
// verifies clean.
constexpr uint64_t PATTERN_MULT = 0x9E3779B97F4A7C15ULL;

inline uint8_t pattern_byte(size_t offset) {
    const uint64_t word = static_cast<uint64_t>(offset / sizeof(uint64_t)) * PATTERN_MULT;
    return static_cast<uint8_t>(word >> (8 * (offset % sizeof(uint64_t))));
}

inline void fill_pattern(uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = pattern_byte(i);
    }
}

// Returns the offset of the first mismatching byte, or `size` if the whole buffer matches. The
// compare covers the entire buffer: checking only a prefix lets a transfer where just the first page
// landed pass.
inline size_t find_pattern_mismatch(const uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buf[i] != pattern_byte(i)) {
            return i;
        }
    }
    return size;
}

// --- command line ----------------------------------------------------------------------------

// Value of the argument following `argv[i]`, advancing `i`. Throws when the flag is trailing:
// `argv[++i]` at i == argc - 1 reads argv[argc], the guaranteed NULL terminator, and constructing a
// std::string from it is UB (in practice a segfault inside strlen, before any usage message).
inline std::string arg_value(int argc, char** argv, int& i) {
    const std::string flag = argv[i];
    if (i + 1 >= argc) {
        throw std::runtime_error(flag + " requires a value");
    }
    return std::string(argv[++i]);
}

// --- RDMA device / port selection ------------------------------------------------------------

// An opened RDMA device plus the port to use on it. The caller owns `ctx` and must ibv_close_device()
// it.
struct RdmaPort {
    ibv_context* ctx = nullptr;
    std::string dev_name;
    int ib_port = 1;
    ibv_port_attr port_attr{};

    bool is_roce() const { return port_attr.link_layer == IBV_LINK_LAYER_ETHERNET; }
};

// Opens an RDMA device and picks a port that is actually usable.
//
// With no --dev/--ib-port override, scans every device and every port 1..phys_port_cnt and takes the
// first in state IBV_PORT_ACTIVE. Taking dev_list[0]/port 1 blindly picks a DOWN NIC on a multi-NIC
// host, which then either reports "no RoCEv2 GID" or brings the QP up on a dead port and times out —
// while UMD's own has_any_active_rdma_port() check has already passed on a *different* device.
//
// An explicit device or port is honoured, but a non-ACTIVE choice is rejected rather than used.
inline RdmaPort open_active_rdma_port(const std::string& requested_dev, int requested_port) {
    int num_devices = 0;
    ibv_device** dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list || num_devices == 0) {
        if (dev_list) {
            ibv_free_device_list(dev_list);
        }
        throw std::runtime_error("No RDMA devices found under /sys/class/infiniband");
    }

    RdmaPort result;
    std::string tried;
    for (int d = 0; d < num_devices && !result.ctx; ++d) {
        const std::string name = ibv_get_device_name(dev_list[d]);
        if (!requested_dev.empty() && name != requested_dev) {
            continue;
        }

        ibv_context* ctx = ibv_open_device(dev_list[d]);
        if (!ctx) {
            tried += " " + name + "(open failed)";
            continue;
        }

        ibv_device_attr dev_attr{};
        if (ibv_query_device(ctx, &dev_attr)) {
            ibv_close_device(ctx);
            tried += " " + name + "(query failed)";
            continue;
        }

        const int first_port = requested_port > 0 ? requested_port : 1;
        const int last_port = requested_port > 0 ? requested_port : dev_attr.phys_port_cnt;
        for (int p = first_port; p <= last_port; ++p) {
            ibv_port_attr port_attr{};
            if (ibv_query_port(ctx, p, &port_attr)) {
                tried += " " + name + ":" + std::to_string(p) + "(query failed)";
                continue;
            }
            if (port_attr.state != IBV_PORT_ACTIVE) {
                tried += " " + name + ":" + std::to_string(p) + "(" + ibv_port_state_str(port_attr.state) + ")";
                continue;
            }
            result.ctx = ctx;
            result.dev_name = name;
            result.ib_port = p;
            result.port_attr = port_attr;
            break;
        }
        if (!result.ctx) {
            ibv_close_device(ctx);
        }
    }
    ibv_free_device_list(dev_list);

    if (!result.ctx) {
        throw std::runtime_error("No RDMA port in the ACTIVE state found; tried:" + (tried.empty() ? " none" : tried));
    }
    std::cout << "Using RDMA device " << result.dev_name << " port " << result.ib_port << " ("
              << ibv_port_state_str(result.port_attr.state) << ", active_mtu=" << (128 << result.port_attr.active_mtu)
              << " B)" << std::endl;
    return result;
}

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

// --- RC QP bring-up --------------------------------------------------------------------------

// Creates an RC QP in the INIT state. `access_flags` is the QP-level permission set: a QP serving
// RDMA_READ needs IBV_ACCESS_REMOTE_READ here even when its MR already grants it, or the read is
// rejected at the QP level.
inline ibv_qp* create_rc_qp(ibv_pd* pd, ibv_cq* cq, int ib_port, uint32_t max_send_wr, int access_flags) {
    ibv_qp_init_attr init_attr{};
    init_attr.send_cq = cq;
    init_attr.recv_cq = cq;
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.cap.max_send_wr = max_send_wr;
    init_attr.cap.max_recv_wr = 1;
    init_attr.cap.max_send_sge = 1;
    init_attr.cap.max_recv_sge = 1;

    ibv_qp* qp = ibv_create_qp(pd, &init_attr);
    if (!qp) {
        throw std::runtime_error(std::string("ibv_create_qp failed: ") + std::strerror(errno));
    }

    ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = static_cast<uint8_t>(ib_port);
    attr.qp_access_flags = access_flags;
    const int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(qp, &attr, flags)) {
        const std::string err = std::strerror(errno);
        ibv_destroy_qp(qp);
        throw std::runtime_error("ibv_modify_qp to INIT failed: " + err);
    }
    return qp;
}

// Moves `qp` INIT -> RTR (pointed at the peer described by `remote`) -> RTS.
//
// path_mtu is negotiated down from the port's active_mtu rather than hardcoded: pinning a
// jumbo-frame fabric (active_mtu 4096) to 1 KiB inflates per-packet header overhead ~4x, so the very
// bandwidth number this example exists to produce comes out silently low — and on a port whose
// active_mtu is below the hardcoded value the RTR transition simply fails.
//
// hop_limit is 64, not 1. This is a cross-host example and the GID selection above deliberately
// prefers the routable IPv4-mapped RoCEv2 GID, so a hop limit of 1 makes every packet die at the
// first router the moment the hosts aren't on one L2 segment — surfacing as "transport retry counter
// exceeded", which is exactly the symptom this file attributes to a RoCEv1 GID above.
inline void connect_rc_qp(
    ibv_qp* qp, const RdmaPort& port, int gid_index, const QpExchangeInfo& remote, uint32_t local_psn) {
    const ibv_mtu mtu = std::min(port.port_attr.active_mtu, IBV_MTU_4096);

    ibv_qp_attr rtr{};
    rtr.qp_state = IBV_QPS_RTR;
    rtr.path_mtu = mtu;
    rtr.dest_qp_num = remote.qpn;
    rtr.rq_psn = remote.psn;
    rtr.max_dest_rd_atomic = 1;
    rtr.min_rnr_timer = 12;
    rtr.ah_attr.port_num = static_cast<uint8_t>(port.ib_port);
    if (port.is_roce()) {
        rtr.ah_attr.is_global = 1;
        std::memcpy(rtr.ah_attr.grh.dgid.raw, remote.gid, 16);
        rtr.ah_attr.grh.sgid_index = static_cast<uint8_t>(gid_index);
        rtr.ah_attr.grh.hop_limit = 64;
    } else {
        rtr.ah_attr.is_global = 0;
        rtr.ah_attr.dlid = remote.lid;
    }
    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(qp, &rtr, flags)) {
        throw std::runtime_error(std::string("ibv_modify_qp to RTR failed: ") + std::strerror(errno));
    }

    ibv_qp_attr rts{};
    rts.qp_state = IBV_QPS_RTS;
    rts.timeout = 14;
    rts.retry_cnt = 7;
    rts.rnr_retry = 7;
    rts.sq_psn = local_psn;
    rts.max_rd_atomic = 1;
    flags =
        IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(qp, &rts, flags)) {
        throw std::runtime_error(std::string("ibv_modify_qp to RTS failed: ") + std::strerror(errno));
    }
}

// Fills in the local half of the handshake for `port`.
inline QpExchangeInfo make_local_exchange_info(const RdmaPort& port, int gid_index, uint32_t qpn, uint32_t psn) {
    QpExchangeInfo info{};
    info.magic = QpExchangeInfo::MAGIC;
    info.qpn = qpn;
    info.psn = psn;
    if (port.is_roce()) {
        ibv_gid gid{};
        if (ibv_query_gid(port.ctx, port.ib_port, gid_index, &gid)) {
            throw std::runtime_error("ibv_query_gid failed");
        }
        std::memcpy(info.gid, gid.raw, 16);
    } else {
        info.lid = port.port_attr.lid;
    }
    return info;
}

inline void check_peer_exchange_info(const QpExchangeInfo& remote) {
    if (remote.magic != QpExchangeInfo::MAGIC) {
        throw std::runtime_error(
            "Handshake magic mismatch (got " + std::to_string(remote.magic) +
            "): the peer is running a different or stale build of this example");
    }
}

// --- out-of-band TCP -------------------------------------------------------------------------

// MSG_NOSIGNAL: without it, a send() to a socket whose peer has died (Ctrl-C, or a failed WQE) kills
// this process with SIGPIPE — no exception, no error message, and no chance to close the exported
// dma-buf and release the TLB window. EINTR is retried rather than treated as a connection error.
inline void send_all(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            throw std::runtime_error(std::string("send_all: connection error: ") + std::strerror(errno));
        }
        sent += static_cast<size_t>(n);
    }
}

inline void recv_all(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n == 0) {
            throw std::runtime_error("recv_all: peer closed the connection");
        }
        if (n < 0) {
            throw std::runtime_error(std::string("recv_all: connection error: ") + std::strerror(errno));
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
        close(listen_fd);
        throw std::runtime_error("bind() failed");
    }
    if (listen(listen_fd, 1) < 0) {
        close(listen_fd);
        throw std::runtime_error("listen() failed");
    }
    int conn_fd = accept(listen_fd, nullptr, nullptr);
    if (conn_fd < 0) {
        close(listen_fd);
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
