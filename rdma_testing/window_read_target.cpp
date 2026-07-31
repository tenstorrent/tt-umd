// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Target side of the exported-TLB-window read-throughput test.
//
// Unlike dmabuf_target.cpp, this tool does NOT go through Cluster::export_dmabuf(). That helper
// allocates a dedicated TlbWindow, hands the fd to the kmd and immediately drops its own handle, so
// nothing on the host is left holding a mapping of the exported window — which makes it impossible to
// read the device *through the very window that was exported*. Here the window is allocated and kept
// alive explicitly:
//
//     PCIDevice::allocate_tlb(window_size, TlbMapping::WC)   -> TlbHandle
//     SiliconTlbWindow(std::move(handle), config)            -> the live window (kept for its lifetime)
//     window->export_dmabuf(0, size)                         -> the fd registered as an RDMA MR
//     window->read_block(0, dst, size)                       -> a device read through THAT window
//
// so both readers — this host's CPU and the peer's NIC — go at device DRAM through one and the same
// TLB window.
//
// Per iteration this process reads --size bytes out of DRAM through the exported window and waits for
// that read to complete, and only then tells the initiator to go ahead with its RDMA_READ over the
// same window. The handshake is a single byte each way on the out-of-band TCP socket that is already
// open for the QP exchange — no extra MRs, receive queues or RNR handling.
#include <infiniband/verbs.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "rdma_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

struct Args {
    uint16_t port = 9999;
    tt::ChipId chip = 0;
    uint64_t addr = 0;
    // 64 MiB, same default as dmabuf_target: comfortably inside Blackhole's 4 GiB DRAM_BANK_SIZE and
    // big enough that per-iteration handshake overhead doesn't dominate. addr + size must stay within
    // a single DRAM bank.
    uint64_t size = 64ull << 20;
    int ib_port = 1;
    int gid_index = -1;  // -1 = auto-detect a RoCEv2 GID (see resolve_gid_index in rdma_common.hpp)
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

// Pick the smallest TLB size class that can cover [addr, addr + size).
//
// A window's NOC base must be size-aligned, so a window of class W aimed at `addr` starts at
// `addr & ~(W-1)` and reaches only `W - (addr % W)` bytes past `addr`. This is the same rule
// Cluster::export_dmabuf() applies internally; it has to be repeated here because this tool drives
// PCIDevice::allocate_tlb() directly, and tt_tlb_alloc() only accepts exact size classes (1/2/16 MiB
// on Wormhole, 2 MiB or 4 GiB on Blackhole).
size_t pick_window_size(const std::vector<size_t>& size_classes, uint64_t addr, uint64_t size) {
    for (const size_t candidate : size_classes) {
        if ((addr % candidate) + size <= candidate) {
            return candidate;
        }
    }
    return 0;
}

}  // namespace

int run(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    // --- UMD side: allocate a TLB window, keep it, and export it ------------------------------
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(args.chip);
    std::vector<CoreCoord> dram_cores = soc_desc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        std::cerr << "No DRAM cores found on chip " << args.chip << std::endl;
        return 1;
    }
    CoreCoord core = dram_cores.front();

    TTDevice* tt_device = cluster->get_tt_device(args.chip);
    PCIDevice* pci_device = tt_device->get_pci_device();
    const std::vector<size_t>& size_classes = tt_device->get_architecture_implementation()->get_tlb_sizes();
    const size_t window_size = pick_window_size(size_classes, args.addr, args.size);
    if (window_size == 0) {
        std::cerr << "No TLB window size class on this arch can cover " << args.size << " bytes at 0x" << std::hex
                  << args.addr << std::dec << "; the largest is " << size_classes.back()
                  << " and its base must be size-aligned. Use a smaller --size or a more aligned --addr." << std::endl;
        return 1;
    }

    // Mirrors the config Cluster::export_dmabuf() builds. `core` already came out of the SocDescriptor
    // in TRANSLATED coords, which is exactly what a TLB config wants, so it goes in as-is. static_vc
    // follows TlbWindow's own rule (set on everything except Blackhole); noc_sel 0 matches the export
    // helper's default NOC.
    tlb_data config{};
    config.local_offset = args.addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = (tt_device->get_arch() != tt::ARCH::BLACKHOLE);

    std::cout << "Allocating TLB window: chip=" << args.chip << " core=(" << core.x << "," << core.y << ") addr=0x"
              << std::hex << args.addr << std::dec << " export_size=" << args.size << " window_class=" << window_size
              << std::endl;

    // WC, matching Cluster::export_dmabuf(). The mapping is kept alive for the whole run: dropping it
    // would fire FREE_TLB, and while the kmd's pin keeps the *export* valid, this process would lose
    // the mapping it needs for read_block().
    std::unique_ptr<TlbWindow> window =
        std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(window_size, TlbMapping::WC), config);

    // Export exactly --size bytes starting at the window's view of `addr`. The window's own offset
    // from its size-aligned base is folded in by TlbWindow::export_dmabuf(), so the dma-buf covers
    // precisely [addr, addr + size) and iova 0 of the peer's MR corresponds to `addr`.
    int dmabuf_fd = window->export_dmabuf(0, args.size);
    std::cout << "Exported the live window as dma-buf fd " << dmabuf_fd << std::endl;

    // Seed the test pattern through a path *independent* of the window under test. Seeding through the
    // window itself would let an address-configuration bug pass: writes and reads would agree with each
    // other while both landing in the wrong place.
    {
        std::vector<uint8_t> seed(args.size);
        for (size_t i = 0; i < args.size; ++i) {
            seed[i] = static_cast<uint8_t>(i & 0xFF);
        }
        cluster->write_to_device(seed.data(), seed.size(), args.chip, core, args.addr);
        std::cout << "Seeded " << args.size << " bytes of test pattern into DRAM via cluster->write_to_device()"
                  << std::endl;
    }

    // --- RDMA side: register the exported window as an MR --------------------------------------
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
    // offset=0, iova=0: the dma-buf has no CPU-side virtual address, so the initiator's remote_addr
    // must also be 0. REMOTE_READ is the flag that matters here — without it the peer's RDMA_READ dies
    // with IBV_WC_REM_ACCESS_ERR.
    ibv_mr* mr = ibv_reg_dmabuf_mr(pd, 0, args.size, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!mr) {
        perror("ibv_reg_dmabuf_mr failed");
        return 1;
    }

    // This side never posts any work: the peer's RDMA_READs are serviced entirely by this NIC, and the
    // per-iteration handshake rides the TCP socket. The QP exists only to be the read target.
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
        // REMOTE_READ must be granted at the QP level too, not just on the MR, or the read is rejected
        // before the MR is ever consulted.
        attr.qp_access_flags = IBV_ACCESS_REMOTE_READ;
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
    // Round count follows the QP info as its own little message rather than being bolted onto
    // QpExchangeInfo, so rdma_common.hpp stays exactly as the existing dmabuf test left it.
    uint64_t iters = 0;
    recv_all(conn, &iters, sizeof(iters));
    std::cout << "Peer QPN=" << remote.qpn << " iters=" << iters << std::endl;

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

    // --- serve loop ---------------------------------------------------------------------------
    //
    // Per iteration: on the initiator's 1-byte 'G', read --size bytes out of DRAM through the exported
    // window (blocking — read_block() does not return until the data is in `host_copy`), then send back
    // a 1-byte 'R' as the go-ahead for the peer's RDMA_READ over that same window.
    std::vector<uint8_t> host_copy(args.size);
    double window_read_s = 0.0;
    bool window_read_ok = true;

    std::cout << "Serving " << iters << " read rounds of " << args.size << " bytes each..." << std::endl;
    for (uint64_t i = 0; i < iters; ++i) {
        char go = 0;
        recv_all(conn, &go, 1);

        // The device read under test: straight through the mapping of the window that is exported as
        // the dma-buf, at the same NOC address the peer's NIC will target.
        auto t0 = std::chrono::steady_clock::now();
        window->read_block(0, host_copy.data(), args.size);
        window_read_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        char ready = 'R';
        send_all(conn, &ready, 1);
    }

    if (iters > 0) {
        double total_bytes = static_cast<double>(args.size) * static_cast<double>(iters);
        std::cout << "Window read (CPU): " << static_cast<uint64_t>(total_bytes) << " bytes in " << window_read_s
                  << " s => " << (total_bytes / window_read_s / 1e9) << " GB/s (DRAM -> this host, through the "
                  << "exported window)" << std::endl;

        // Check the last read. A mismatch *here* localizes the fault to the window's configuration or
        // the UMD read path, before the NIC or the network is implicated at all.
        for (size_t i = 0; i < args.size && i < 4096; ++i) {
            uint8_t expected = static_cast<uint8_t>(i & 0xFF);
            if (host_copy[i] != expected) {
                std::cerr << "Window read mismatch at byte " << i << ": expected " << static_cast<int>(expected)
                          << " got " << static_cast<int>(host_copy[i]) << std::endl;
                window_read_ok = false;
                break;
            }
        }
        std::cout << (window_read_ok ? "PASS" : "FAIL") << " (window read on this host; the initiator reports the "
                  << "NIC read over the same window)" << std::endl;
    }

    // Wait for the initiator to report it is finished before tearing the window down.
    char done = 0;
    recv_all(conn, &done, 1);

    close(conn);
    // Order matters: deregister the MR and close the fd before the window (and its mapping) go away.
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    close(dmabuf_fd);
    window.reset();
    return window_read_ok ? 0 : 1;
}

int main(int argc, char** argv) {
    // recv_all()/send_all() throw on a dropped connection (e.g. the initiator dying mid-run), and the
    // UMD calls throw UMD_THROW on device errors; catch here so a failure prints a clean error instead
    // of an uncaught-exception core dump.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
