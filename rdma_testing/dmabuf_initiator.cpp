// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// RDMA initiator for the two-host dma-buf bandwidth test. Registers a normal host-memory MR filled
// with a known test pattern, brings up an RC QP against the target, and issues repeated RDMA WRITEs
// into the target's dma-buf-backed MR (see dmabuf_target.cpp) to measure sustained write bandwidth.
// No UMD dependency here at all.
#include <infiniband/verbs.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "rdma_common.hpp"

namespace {

struct Args {
    std::string host;
    uint16_t port = 9999;
    // 64 MiB: must match dmabuf_target's --size (Blackhole DRAM_BANK_SIZE is 4 GiB, so there's
    // plenty of headroom vs the old Tensix-L1-backed 1.5 MiB ceiling).
    uint64_t size = 64ull << 20;
    uint64_t iters = 100;  // number of RDMA_WRITEs of --size bytes to issue, timed as one batch
    int ib_port = 1;
    int gid_index = -1;  // -1 = auto-detect a RoCEv2 GID (see resolve_gid_index in rdma_common.hpp)
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--host") {
            a.host = next();
        } else if (arg == "--port") {
            a.port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--size") {
            a.size = std::stoull(next());
        } else if (arg == "--iters") {
            a.iters = std::stoull(next());
        } else if (arg == "--ib-port") {
            a.ib_port = std::stoi(next());
        } else if (arg == "--gid-index") {
            a.gid_index = std::stoi(next());
        }
    }
    return a;
}

// Number of in-flight, unsignaled RDMA_WRITEs between each polled completion. RC QPs complete
// WQEs in order, so polling one CQE every SIGNAL_INTERVAL writes still confirms all of them landed.
// Kept at 1 (fully synchronous, one WRITE in flight at a time): at the default 64 MiB --size,
// ibv_poll_cq overhead is negligible next to per-message transfer time, so there's nothing to gain
// from batching completions — and batching here is actively dangerous, since going over 32 previously
// left ~2 GiB of unpaced, unacknowledged WRITEs outstanding and blew through the QP's retry budget
// ("transport retry counter exceeded") on a real fabric. Only raise this if you've confirmed the
// fabric tolerates more bytes in flight for your chosen --size.
constexpr uint64_t SIGNAL_INTERVAL = 1;

}  // namespace

int run(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    if (args.host.empty()) {
        std::cerr << "Usage: dmabuf_initiator --host <target-host> --port <port> [--size N]" << std::endl;
        return 1;
    }

    // Test pattern: byte i = i & 0xFF, so the target can verify without needing to share the
    // buffer contents out of band.
    std::vector<uint8_t> local_buf(args.size);
    for (size_t i = 0; i < args.size; ++i) {
        local_buf[i] = static_cast<uint8_t>(i & 0xFF);
    }

    int num_devices = 0;
    ibv_device** dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list || num_devices == 0) {
        std::cerr << "No RDMA devices found" << std::endl;
        return 1;
    }
    ibv_context* ctx = ibv_open_device(dev_list[0]);
    ibv_free_device_list(dev_list);
    if (!ctx) {
        std::cerr << "ibv_open_device failed" << std::endl;
        return 1;
    }

    ibv_port_attr port_attr{};
    if (ibv_query_port(ctx, args.ib_port, &port_attr)) {
        std::cerr << "ibv_query_port failed" << std::endl;
        return 1;
    }
    bool is_roce = (port_attr.link_layer == IBV_LINK_LAYER_ETHERNET);
    if (is_roce) {
        args.gid_index = resolve_gid_index(ctx, args.ib_port, args.gid_index);
    }

    ibv_pd* pd = ibv_alloc_pd(ctx);
    ibv_mr* mr = ibv_reg_mr(pd, local_buf.data(), local_buf.size(), IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        perror("ibv_reg_mr failed");
        return 1;
    }

    ibv_cq* cq = ibv_create_cq(ctx, 1, nullptr, nullptr, 0);
    ibv_qp_init_attr qp_init_attr{};
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    // Must hold at least SIGNAL_INTERVAL WQEs, since that many go unsignaled (and thus unretired)
    // between polls.
    qp_init_attr.cap.max_send_wr = static_cast<uint32_t>(SIGNAL_INTERVAL);
    qp_init_attr.cap.max_recv_wr = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    ibv_qp* qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        std::cerr << "ibv_create_qp failed" << std::endl;
        return 1;
    }

    {
        ibv_qp_attr attr{};
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = args.ib_port;
        attr.qp_access_flags = 0;
        int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
        if (ibv_modify_qp(qp, &attr, flags)) {
            std::cerr << "Failed to move QP to INIT" << std::endl;
            return 1;
        }
    }

    QpExchangeInfo local{};
    local.qpn = qp->qp_num;
    local.psn = 0x5678;
    if (is_roce) {
        ibv_gid gid{};
        if (ibv_query_gid(ctx, args.ib_port, args.gid_index, &gid)) {
            std::cerr << "ibv_query_gid failed" << std::endl;
            return 1;
        }
        std::memcpy(local.gid, gid.raw, 16);
    } else {
        local.lid = port_attr.lid;
    }

    std::cout << "Connecting to " << args.host << ":" << args.port << "..." << std::endl;
    int conn = tcp_connect(args.host, args.port);
    QpExchangeInfo remote{};
    recv_all(conn, &remote, sizeof(remote));
    send_all(conn, &local, sizeof(local));
    std::cout << "Target QPN=" << remote.qpn << " rkey=" << remote.rkey << std::endl;

    {
        ibv_qp_attr attr{};
        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = IBV_MTU_1024;
        attr.dest_qp_num = remote.qpn;
        attr.rq_psn = remote.psn;
        attr.max_dest_rd_atomic = 1;
        attr.min_rnr_timer = 12;
        attr.ah_attr.port_num = args.ib_port;
        if (is_roce) {
            attr.ah_attr.is_global = 1;
            std::memcpy(attr.ah_attr.grh.dgid.raw, remote.gid, 16);
            attr.ah_attr.grh.sgid_index = args.gid_index;
            attr.ah_attr.grh.hop_limit = 1;
        } else {
            attr.ah_attr.is_global = 0;
            attr.ah_attr.dlid = remote.lid;
        }
        int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
        if (ibv_modify_qp(qp, &attr, flags)) {
            std::cerr << "Failed to move QP to RTR" << std::endl;
            return 1;
        }
    }
    {
        ibv_qp_attr attr{};
        attr.qp_state = IBV_QPS_RTS;
        attr.timeout = 14;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7;
        attr.sq_psn = local.psn;
        attr.max_rd_atomic = 1;
        int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                    IBV_QP_MAX_QP_RD_ATOMIC;
        if (ibv_modify_qp(qp, &attr, flags)) {
            std::cerr << "Failed to move QP to RTS" << std::endl;
            return 1;
        }
    }

    // Repeatedly RDMA WRITE local_buf -> target's dma-buf MR at iova 0 (see dmabuf_target.cpp; the
    // dma-buf MR was registered with iova=0, so remote_addr here must also be 0). Every write lands
    // in the same remote window, which is fine for a bandwidth measurement — only the last one's
    // content is checked, and each write reproduces the identical pattern.
    ibv_sge sge{};
    sge.addr = reinterpret_cast<uint64_t>(local_buf.data());
    sge.length = static_cast<uint32_t>(local_buf.size());
    sge.lkey = mr->lkey;

    ibv_send_wr wr{};
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.wr.rdma.remote_addr = remote.remote_addr;
    wr.wr.rdma.rkey = remote.rkey;

    std::cout << "Issuing " << args.iters << " x " << args.size << " byte RDMA WRITEs..." << std::endl;
    auto t_start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < args.iters; ++i) {
        bool signal = ((i + 1) % SIGNAL_INTERVAL == 0) || (i + 1 == args.iters);
        wr.wr_id = i;
        wr.send_flags = signal ? IBV_SEND_SIGNALED : 0;

        ibv_send_wr* bad_wr = nullptr;
        if (ibv_post_send(qp, &wr, &bad_wr)) {
            std::cerr << "ibv_post_send failed at iter " << i << std::endl;
            return 1;
        }

        if (signal) {
            ibv_wc wc{};
            int n = 0;
            while (n == 0) {
                n = ibv_poll_cq(cq, 1, &wc);
                if (n < 0) {
                    std::cerr << "ibv_poll_cq failed" << std::endl;
                    return 1;
                }
            }
            if (wc.status != IBV_WC_SUCCESS) {
                // wc.wr_id, not the loop counter i: once a fatal transport error hits, the QP flushes
                // all outstanding WQEs, so the CQE we get back may belong to an earlier, still-unsignaled
                // write rather than the one posted this iteration.
                std::cerr << "RDMA WRITE failed, wr_id=" << wc.wr_id << ": " << ibv_wc_status_str(wc.status)
                          << std::endl;
                return 1;
            }
        }
    }
    auto t_end = std::chrono::steady_clock::now();

    double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
    double total_bytes = static_cast<double>(args.size) * static_cast<double>(args.iters);
    double gbps = total_bytes / elapsed_s / 1e9;
    std::cout << "Wrote " << static_cast<uint64_t>(total_bytes) << " bytes in " << elapsed_s << " s => " << gbps
              << " GB/s" << std::endl;

    // Signal the target that the write has landed (it's a real hardware ACK from the CQE above,
    // not just "posted") so it's safe to read back and verify.
    char done = 1;
    send_all(conn, &done, 1);

    close(conn);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    return 0;
}

int main(int argc, char** argv) {
    // tcp_connect()/send_all()/recv_all() and resolve_gid_index() throw; catch here so a
    // handshake or config failure prints a clean error instead of an uncaught-exception core dump.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
