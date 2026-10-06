// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tests/rdma/rdma_loopback.hpp"

#include <fcntl.h>
#include <infiniband/verbs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace tt::umd::test {

namespace {

// The 12 bytes an IPv4-mapped GID starts with; RoCEv2 over IPv4 uses exactly this form.
constexpr uint8_t V4_MAPPED_PREFIX[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

// True when GID `index` of `device`/`port` is a RoCEv2 GID.
//
// Input:  RDMA device name (e.g. "rocep1s0"), 1-based port number, GID index.
// Output: true if the kernel reports that GID's type as "RoCE v2".
// Reads sysfs only; no hardware state is changed.
bool gid_is_rocev2(const char* device, uint8_t port, int index) {
    char path[256];
    char buf[64];

    snprintf(path, sizeof(path), "/sys/class/infiniband/%s/ports/%u/gid_attrs/types/%d", device, port, index);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }
    buf[n] = '\0';
    return strstr(buf, "RoCE v2") != nullptr;
}

}  // namespace

RdmaLoopback::RdmaLoopback(int max_outstanding_reads) : max_outstanding_reads_(std::max(max_outstanding_reads, 1)) {
    open_first_active_device();

    pd_ = ibv_alloc_pd(ctx_);
    if (!pd_) {
        throw std::runtime_error("ibv_alloc_pd failed");
    }

    // Sized for both queue pairs' completions even though only the requester generates any: RDMA READ
    // is one-sided, so the responder produces no completion entry.
    cq_ = ibv_create_cq(ctx_, max_outstanding_reads_ * 2 + 16, nullptr, nullptr, 0);
    if (!cq_) {
        throw std::runtime_error("ibv_create_cq failed");
    }

    select_rocev2_gid();
    query_rd_atomic_limits(max_outstanding_reads_);

    // Both ends of a connection must agree on the MTU; the port's active MTU is the ceiling.
    enum ibv_mtu mtu = IBV_MTU_4096;

    struct ibv_port_attr port_attr {};

    if (!ibv_query_port(ctx_, port_, &port_attr)) {
        mtu = std::min(port_attr.active_mtu, mtu);
    }
    path_mtu_ = static_cast<int>(mtu);

    const int sq_depth = max_outstanding_reads_ + 8;
    qp_requester_ = create_qp(sq_depth);
    qp_responder_ = create_qp(sq_depth);

    const uint32_t psn_requester = 0x111111;
    const uint32_t psn_responder = 0x222222;
    connect_qp(qp_requester_, qp_responder_->qp_num, psn_responder, psn_requester);
    connect_qp(qp_responder_, qp_requester_->qp_num, psn_requester, psn_responder);
}

RdmaLoopback::~RdmaLoopback() {
    if (local_mr_) {
        ibv_dereg_mr(local_mr_);
    }
    if (dmabuf_mr_) {
        ibv_dereg_mr(dmabuf_mr_);
    }
    if (qp_requester_) {
        ibv_destroy_qp(qp_requester_);
    }
    if (qp_responder_) {
        ibv_destroy_qp(qp_responder_);
    }
    if (cq_) {
        ibv_destroy_cq(cq_);
    }
    if (pd_) {
        ibv_dealloc_pd(pd_);
    }
    if (ctx_) {
        ibv_close_device(ctx_);
    }
}

void RdmaLoopback::open_first_active_device() {
    int num_devices = 0;
    struct ibv_device** list = ibv_get_device_list(&num_devices);
    if (!list) {
        throw std::runtime_error("ibv_get_device_list failed");
    }

    for (int i = 0; i < num_devices && !ctx_; i++) {
        struct ibv_context* candidate = ibv_open_device(list[i]);
        if (!candidate) {
            continue;
        }

        struct ibv_device_attr dev_attr {};

        if (ibv_query_device(candidate, &dev_attr)) {
            ibv_close_device(candidate);
            continue;
        }
        for (uint8_t port = 1; port <= dev_attr.phys_port_cnt; port++) {
            struct ibv_port_attr port_attr {};

            if (!ibv_query_port(candidate, port, &port_attr) && port_attr.state == IBV_PORT_ACTIVE) {
                ctx_ = candidate;
                device_name_ = ibv_get_device_name(list[i]);
                port_ = port;
                break;
            }
        }
        if (!ctx_) {
            ibv_close_device(candidate);
        }
    }
    ibv_free_device_list(list);

    if (!ctx_) {
        throw std::runtime_error("no RDMA device with an ACTIVE port found");
    }
}

void RdmaLoopback::select_rocev2_gid() {
    union ibv_gid gid {};

    for (int i = 0; i < 16; i++) {
        if (ibv_query_gid(ctx_, port_, i, &gid)) {
            break;
        }
        if (memcmp(gid.raw, V4_MAPPED_PREFIX, sizeof(V4_MAPPED_PREFIX)) != 0) {
            continue;
        }
        if (gid_is_rocev2(device_name_.c_str(), port_, i)) {
            gid_index_ = i;
            memcpy(gid_raw_, gid.raw, sizeof(gid_raw_));
            return;
        }
    }
    throw std::runtime_error("no RoCEv2 (IPv4-mapped) GID found on " + device_name_);
}

void RdmaLoopback::query_rd_atomic_limits(int max_outstanding_reads) {
    struct ibv_device_attr dev_attr {};

    int hw_init = 16;
    int hw_dest = 16;
    if (ibv_query_device(ctx_, &dev_attr) == 0) {
        if (dev_attr.max_qp_init_rd_atom > 0) {
            hw_init = dev_attr.max_qp_init_rd_atom;
        }
        if (dev_attr.max_qp_rd_atom > 0) {
            hw_dest = dev_attr.max_qp_rd_atom;
        }
    }
    // Both attributes are carried in uint8_t fields of ibv_qp_attr, hence the 255 cap.
    rd_atomic_init_ = std::min({max_outstanding_reads, hw_init, 255});
    rd_atomic_dest_ = std::min({max_outstanding_reads, hw_dest, 255});
}

ibv_qp* RdmaLoopback::create_qp(int sq_depth) {
    struct ibv_qp_init_attr init_attr {};

    init_attr.send_cq = cq_;
    init_attr.recv_cq = cq_;
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.cap.max_send_wr = static_cast<uint32_t>(sq_depth);
    init_attr.cap.max_recv_wr = 4;
    init_attr.cap.max_send_sge = 1;
    init_attr.cap.max_recv_sge = 1;

    struct ibv_qp* qp = ibv_create_qp(pd_, &init_attr);
    if (!qp) {
        throw std::runtime_error(std::string("ibv_create_qp failed: ") + strerror(errno));
    }

    struct ibv_qp_attr attr {};

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = port_;
    // Both queue pairs get the full set: the responder needs REMOTE_READ to serve reads out of the
    // dma-buf memory region, and granting it symmetrically keeps the two setup paths identical.
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        ibv_destroy_qp(qp);
        throw std::runtime_error(std::string("ibv_modify_qp to INIT failed: ") + strerror(errno));
    }
    return qp;
}

void RdmaLoopback::connect_qp(ibv_qp* qp, uint32_t dest_qpn, uint32_t dest_psn, uint32_t local_psn) {
    struct ibv_qp_attr rtr {};

    rtr.qp_state = IBV_QPS_RTR;
    rtr.path_mtu = static_cast<enum ibv_mtu>(path_mtu_);
    rtr.dest_qp_num = dest_qpn;
    rtr.rq_psn = dest_psn;
    rtr.max_dest_rd_atomic = static_cast<uint8_t>(rd_atomic_dest_);
    rtr.min_rnr_timer = 12;
    rtr.ah_attr.is_global = 1;
    rtr.ah_attr.port_num = port_;
    rtr.ah_attr.grh.sgid_index = gid_index_;
    rtr.ah_attr.grh.hop_limit = 64;
    // Destination GID == our own GID: this is what makes the connection a loopback.
    memcpy(rtr.ah_attr.grh.dgid.raw, gid_raw_, sizeof(gid_raw_));

    if (ibv_modify_qp(
            qp,
            &rtr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER)) {
        throw std::runtime_error(std::string("ibv_modify_qp to RTR failed: ") + strerror(errno));
    }

    struct ibv_qp_attr rts {};

    rts.qp_state = IBV_QPS_RTS;
    rts.timeout = 14;
    rts.retry_cnt = 7;
    rts.rnr_retry = 7;
    rts.sq_psn = local_psn;
    rts.max_rd_atomic = static_cast<uint8_t>(rd_atomic_init_);
    if (ibv_modify_qp(
            qp,
            &rts,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC)) {
        throw std::runtime_error(std::string("ibv_modify_qp to RTS failed: ") + strerror(errno));
    }
}

void RdmaLoopback::register_local_sink(void* addr, size_t size) {
    if (local_mr_) {
        throw std::runtime_error("local sink already registered");
    }
    local_mr_ = ibv_reg_mr(pd_, addr, size, IBV_ACCESS_LOCAL_WRITE);
    if (!local_mr_) {
        throw std::runtime_error(std::string("ibv_reg_mr failed: ") + strerror(errno));
    }
    local_addr_ = addr;
    local_size_ = size;
}

void RdmaLoopback::register_dmabuf_source(int dmabuf_fd, size_t size) {
    if (dmabuf_mr_) {
        throw std::runtime_error("dma-buf source already registered");
    }
    // IOVA 0, so a remote address on the READ is a plain byte offset into the exported window.
    dmabuf_mr_ = ibv_reg_dmabuf_mr(
        pd_, 0, size, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!dmabuf_mr_) {
        throw std::runtime_error(std::string("ibv_reg_dmabuf_mr failed: ") + strerror(errno));
    }
    dmabuf_size_ = size;
}

void RdmaLoopback::read(size_t size, size_t chunk_size) {
    if (!local_mr_ || !dmabuf_mr_) {
        throw std::runtime_error("read() requires both a local sink and a dma-buf source");
    }
    if (size > local_size_ || size > dmabuf_size_) {
        throw std::runtime_error("read() size exceeds a registered memory region");
    }
    if (chunk_size == 0) {
        throw std::runtime_error("read() chunk_size must be non-zero");
    }

    const uintptr_t host_base = reinterpret_cast<uintptr_t>(local_addr_);
    size_t offset = 0;
    int inflight = 0;

    while (offset < size || inflight) {
        while (offset < size && inflight < max_outstanding_reads_) {
            size_t len = std::min(size - offset, chunk_size);

            struct ibv_sge sge {};

            sge.addr = static_cast<uint64_t>(host_base + offset);
            sge.length = static_cast<uint32_t>(len);
            sge.lkey = local_mr_->lkey;

            struct ibv_send_wr wr {};

            wr.wr_id = offset;
            wr.sg_list = &sge;
            wr.num_sge = 1;
            wr.opcode = IBV_WR_RDMA_READ;
            wr.send_flags = IBV_SEND_SIGNALED;
            wr.wr.rdma.remote_addr = offset;
            wr.wr.rdma.rkey = dmabuf_mr_->rkey;

            struct ibv_send_wr* bad = nullptr;
            if (int rc = ibv_post_send(qp_requester_, &wr, &bad)) {
                throw std::runtime_error(std::string("ibv_post_send failed: ") + strerror(rc));
            }
            offset += len;
            inflight++;
        }

        struct ibv_wc wc {};

        int n = ibv_poll_cq(cq_, 1, &wc);
        if (n < 0) {
            throw std::runtime_error("ibv_poll_cq failed");
        }
        if (n > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                throw std::runtime_error(
                    std::string("RDMA READ completed with error: ") + ibv_wc_status_str(wc.status));
            }
            inflight--;
        }
    }
}

}  // namespace tt::umd::test
