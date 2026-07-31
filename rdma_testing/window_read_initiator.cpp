// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Initiator side of the exported-TLB-window read-throughput test. See window_read_target.cpp for the
// half that owns the window.
//
// Each iteration is:
//   1. send 1-byte 'G' over the out-of-band TCP socket  -> target reads --size bytes out of device DRAM
//                                                          through the exported TLB window and blocks
//                                                          until that read has completed
//   2. receive the target's 1-byte 'R'                  -> the device read demonstrably finished
//   3. post IBV_WR_RDMA_READ against the target's dma-buf MR, which is that same exported window, and
//      poll its completion                              -> this NIC pulls the bytes over the network
//
// Two numbers come out of it: the full round trip, and step 3 in isolation (the NIC read over the
// exported window), which is the read throughput that usually matters. No UMD dependency here at all.
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
    uint64_t size = 64ull << 20;  // must match window_read_target's --size
    uint64_t iters = 100;         // number of read rounds, timed as one batch
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

}  // namespace

int run(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    if (args.host.empty()) {
        std::cerr << "Usage: window_read_initiator --host <target-host> [--port N] [--size N] [--iters N]"
                  << " [--ib-port N] [--gid-index N]" << std::endl;
        return 1;
    }

    // Prefill with a sentinel that is NOT the expected pattern, so a read that silently moved nothing
    // cannot false-pass. The target seeds the real pattern into DRAM before the handshake.
    std::vector<uint8_t> local_buf(args.size, 0xAA);

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
    // LOCAL_WRITE is what lets the NIC deposit RDMA_READ data into this buffer. Nothing remote ever
    // writes here, so no REMOTE_* flag is needed.
    ibv_mr* mr = ibv_reg_mr(pd, local_buf.data(), local_buf.size(), IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        perror("ibv_reg_mr failed");
        return 1;
    }

    ibv_cq* cq = ibv_create_cq(ctx, 4, nullptr, nullptr, 0);
    ibv_qp_init_attr qp_init_attr{};
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    // One RDMA_READ in flight at a time: the loop is inherently serialized by the per-iteration
    // handshake, so there is nothing to pipeline and nothing to gain from a deeper send queue.
    qp_init_attr.cap.max_send_wr = 1;
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
    // Round count as its own little message right behind the QP info, so rdma_common.hpp stays exactly
    // as the existing dmabuf test left it.
    send_all(conn, &args.iters, sizeof(args.iters));
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

    // The dma-buf MR was registered with iova=0, so remote_addr is 0 and maps to the target's --addr.
    ibv_sge sge{};
    sge.addr = reinterpret_cast<uint64_t>(local_buf.data());
    sge.length = static_cast<uint32_t>(local_buf.size());
    sge.lkey = mr->lkey;

    ibv_send_wr wr{};
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote.remote_addr;
    wr.wr.rdma.rkey = remote.rkey;

    std::cout << "Running " << args.iters << " x " << args.size << " byte read rounds..." << std::endl;
    double nic_read_s = 0.0;
    auto t_start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < args.iters; ++i) {
        // Step 1/2: make the target pull the data out of DRAM through the exported window first, and
        // wait for it to say that read finished. One byte each way on the socket already open for the
        // QP exchange — the whole handshake.
        char go = 'G';
        send_all(conn, &go, 1);
        char ready = 0;
        recv_all(conn, &ready, 1);
        if (ready != 'R') {
            std::cerr << "Unexpected handshake byte from target at iter " << i << ": " << static_cast<int>(ready)
                      << std::endl;
            return 1;
        }

        // Step 3: now this NIC reads the same window over the network. Timed separately, since this is
        // the number the round trip exists to produce.
        auto t0 = std::chrono::steady_clock::now();
        wr.wr_id = i;
        ibv_send_wr* bad_wr = nullptr;
        if (ibv_post_send(qp, &wr, &bad_wr)) {
            std::cerr << "ibv_post_send failed at iter " << i << std::endl;
            return 1;
        }

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
            std::cerr << "RDMA READ failed at iter " << i << ": " << ibv_wc_status_str(wc.status) << std::endl;
            return 1;
        }
        nic_read_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    auto t_end = std::chrono::steady_clock::now();

    bool all_match = true;
    if (args.iters > 0) {
        double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
        double total_bytes = static_cast<double>(args.size) * static_cast<double>(args.iters);
        std::cout << "NIC read over the exported window: " << static_cast<uint64_t>(total_bytes) << " bytes in "
                  << nic_read_s << " s => " << (total_bytes / nic_read_s / 1e9) << " GB/s" << std::endl;
        std::cout << "Full round trip (target window read + handshake + NIC read): " << elapsed_s << " s => "
                  << (total_bytes / elapsed_s / 1e9) << " GB/s" << std::endl;

        // The pattern the target seeded into DRAM has to be what arrived here, and it must not still be
        // the 0xAA sentinel.
        for (size_t i = 0; i < args.size && i < 4096; ++i) {
            uint8_t expected = static_cast<uint8_t>(i & 0xFF);
            if (local_buf[i] != expected) {
                std::cerr << "Mismatch at byte " << i << ": expected " << static_cast<int>(expected) << " got "
                          << static_cast<int>(local_buf[i]) << std::endl;
                all_match = false;
                break;
            }
        }
        std::cout << (all_match ? "PASS" : "FAIL") << " (NIC read over the exported window)" << std::endl;
    }

    // Let the target tear its window down only once this side is finished with it.
    char done = 1;
    send_all(conn, &done, 1);

    close(conn);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    return all_match ? 0 : 1;
}

int main(int argc, char** argv) {
    // tcp_connect()/send_all()/recv_all() and resolve_gid_index() throw; catch here so a handshake or
    // config failure prints a clean error instead of an uncaught-exception core dump.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
