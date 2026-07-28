// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// SPDX-License-Identifier: Apache-2.0
//
// RDMA target for the two-host dma-buf smoke test. Exports a TLB window via UMD's
// Cluster::export_dmabuf(), registers the resulting fd as an RDMA MR with ibv_reg_dmabuf_mr(), and
// waits for a peer NIC to RDMA-WRITE into it. Verifies the write landed by reading the same NOC
// address back through UMD (a path independent of the exported window).
#include <infiniband/verbs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "rdma_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// TLB windows can only be allocated in the sizes tt_tlb_alloc() supports (see
// device/api/umd/device/tt_kmd_lib/tt_kmd_lib.h): 1/2/16 MiB on Wormhole, 2 MiB or 4 GiB on
// Blackhole. 1 MiB is not a valid window size on Blackhole (tt_tlb_alloc fails with -EINVAL), so
// the window is always allocated at 2 MiB regardless of --size; --size only controls how many
// bytes of that window get registered as the RDMA MR and verified.
constexpr uint64_t kTlbWindowSize = 2ull << 20;

struct Args {
    uint16_t port = 9999;
    tt::ChipId chip = 0;
    uint64_t addr = 0;
    uint64_t size = 1ull << 20;  // 1 MiB — must stay under Blackhole TENSIX_L1_SIZE (1.5 MiB)
    int ib_port = 1;
    int gid_index = 0;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--port") {
            a.port = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--chip") {
            a.chip = std::stoi(next());
        } else if (arg == "--addr") {
            a.addr = std::stoull(next(), nullptr, 0);
        } else if (arg == "--size") {
            a.size = std::stoull(next());
        } else if (arg == "--ib-port") {
            a.ib_port = std::stoi(next());
        } else if (arg == "--gid-index") {
            a.gid_index = std::stoi(next());
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    // --- UMD side: export a TLB window as a dma-buf ------------------------------------------
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(args.chip);
    std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    if (tensix_cores.empty()) {
        std::cerr << "No tensix cores found on chip " << args.chip << std::endl;
        return 1;
    }
    CoreCoord core = tensix_cores.front();

    std::cout << "Exporting TLB window: chip=" << args.chip << " core=(" << core.x << "," << core.y << ") addr=0x"
              << std::hex << args.addr << std::dec << " size=" << args.size << std::endl;

    if (args.size > kTlbWindowSize) {
        std::cerr << "--size " << args.size << " exceeds the TLB window size " << kTlbWindowSize << std::endl;
        return 1;
    }
    int dmabuf_fd = cluster->export_dmabuf(args.chip, core, args.addr, kTlbWindowSize);
    std::cout << "Got dma-buf fd " << dmabuf_fd << std::endl;

    // --- RDMA side: register the dma-buf as an MR --------------------------------------------
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

    ibv_pd* pd = ibv_alloc_pd(ctx);
    // offset=0, iova=0: the dma-buf has no CPU-side virtual address, so remote_addr on the
    // initiator's WRITE must also be 0 to match this MR's iova.
    ibv_mr* mr = ibv_reg_dmabuf_mr(pd, 0, args.size, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        perror("ibv_reg_dmabuf_mr failed");
        return 1;
    }

    ibv_cq* cq = ibv_create_cq(ctx, 1, nullptr, nullptr, 0);
    ibv_qp_init_attr qp_init_attr{};
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = 1;
    qp_init_attr.cap.max_recv_wr = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    ibv_qp* qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        std::cerr << "ibv_create_qp failed" << std::endl;
        return 1;
    }

    // INIT
    {
        ibv_qp_attr attr{};
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = args.ib_port;
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
        int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
        if (ibv_modify_qp(qp, &attr, flags)) {
            std::cerr << "Failed to move QP to INIT" << std::endl;
            return 1;
        }
    }

    QpExchangeInfo local{};
    local.qpn = qp->qp_num;
    local.psn = 0x1234;
    local.rkey = mr->rkey;
    local.remote_addr = 0;
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

    // --- OOB handshake -------------------------------------------------------------------------
    std::cout << "Waiting for initiator on port " << args.port << "..." << std::endl;
    int conn = tcp_listen_accept(args.port);
    QpExchangeInfo remote{};
    send_all(conn, &local, sizeof(local));
    recv_all(conn, &remote, sizeof(remote));
    std::cout << "Peer QPN=" << remote.qpn << std::endl;

    // RTR
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
    // RTS
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

    // Wait for the initiator to signal it has posted (and completed) the RDMA WRITE.
    char done = 0;
    recv_all(conn, &done, 1);
    std::cout << "Initiator signaled write complete; verifying via UMD readback..." << std::endl;

    std::vector<uint8_t> readback(args.size);
    cluster->read_from_device(readback.data(), args.chip, core, args.addr, args.size);

    bool all_match = true;
    for (size_t i = 0; i < args.size && i < 4096; ++i) {
        // Initiator fills its buffer with pattern byte = (i & 0xFF); check the first 4KiB, enough
        // to catch a wrong-address / wrong-fd / stale-data class of bug without a slow full compare.
        uint8_t expected = static_cast<uint8_t>(i & 0xFF);
        if (readback[i] != expected) {
            std::cerr << "Mismatch at byte " << i << ": expected " << static_cast<int>(expected) << " got "
                      << static_cast<int>(readback[i]) << std::endl;
            all_match = false;
            break;
        }
    }

    std::cout << (all_match ? "PASS" : "FAIL") << std::endl;

    close(conn);
    close(dmabuf_fd);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    return all_match ? 0 : 1;
}
