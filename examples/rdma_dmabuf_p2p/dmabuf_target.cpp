// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// RDMA target for the two-host dma-buf bandwidth test. Exports a TLB window via UMD's
// Cluster::export_dmabuf(), registers the resulting fd as an RDMA MR with ibv_reg_dmabuf_mr(), and
// waits for a peer NIC to RDMA-WRITE into it (repeatedly, to measure sustained bandwidth). Verifies
// the final write landed by reading the same NOC address back through UMD (a path independent of
// the exported window).
#include <infiniband/verbs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "rdma_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
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
    // 2 MiB: this example targets Blackhole, whose only TLB window size classes are 2 MiB and 4 GiB
    // (see the size class tables in device/arch/architecture_tlbs.cpp). export_dmabuf() rounds up to
    // the next class that can cover the request, so any larger default quietly consumes one of the
    // few 4 GiB windows. Raise it with --size only when that trade is what you want.
    uint64_t size = 2ull << 20;
    std::string dev;                   // empty = first RDMA device with an ACTIVE port
    int ib_port = -1;                  // -1 = first ACTIVE port on that device
    int gid_index = -1;                // -1 = auto-detect a RoCEv2 GID (see resolve_gid_index in rdma_common.hpp)
    std::string op = "write";          // must match the initiator's --op: decides who seeds and who verifies
    std::string ordering = "relaxed";  // TLB window ordering mode for the export
};

void print_usage() {
    std::cerr << "Usage: dmabuf_target [--port N] [--chip N] [--addr 0xN] [--size N] [--dev NAME]\n"
              << "                     [--ib-port N] [--gid-index N] [--op write|read]\n"
              << "                     [--ordering relaxed|posted|strict]" << std::endl;
}

uint64_t parse_ordering(const std::string& name) {
    if (name == "relaxed") {
        return tlb_data::Relaxed;
    }
    if (name == "posted") {
        return tlb_data::Posted;
    }
    if (name == "strict") {
        return tlb_data::Strict;
    }
    throw std::runtime_error("--ordering must be relaxed, posted or strict, got '" + name + "'");
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port") {
            a.port = static_cast<uint16_t>(std::stoi(arg_value(argc, argv, i)));
        } else if (arg == "--chip") {
            a.chip = std::stoi(arg_value(argc, argv, i));
        } else if (arg == "--addr") {
            a.addr = std::stoull(arg_value(argc, argv, i), nullptr, 0);
        } else if (arg == "--size") {
            a.size = std::stoull(arg_value(argc, argv, i));
        } else if (arg == "--dev") {
            a.dev = arg_value(argc, argv, i);
        } else if (arg == "--ib-port") {
            a.ib_port = std::stoi(arg_value(argc, argv, i));
        } else if (arg == "--gid-index") {
            a.gid_index = std::stoi(arg_value(argc, argv, i));
        } else if (arg == "--op") {
            a.op = arg_value(argc, argv, i);
        } else if (arg == "--ordering") {
            a.ordering = arg_value(argc, argv, i);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument '" + arg + "'");
        }
    }
    return a;
}

}  // namespace

int run(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    if (args.op != "write" && args.op != "read") {
        std::cerr << "--op must be 'write' or 'read', got '" << args.op << "'" << std::endl;
        print_usage();
        return 1;
    }
    const bool is_read = (args.op == "read");
    const uint64_t ordering = parse_ordering(args.ordering);

    // --- UMD side: export a TLB window as a dma-buf ------------------------------------------
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(args.chip);
    std::vector<CoreCoord> dram_cores = soc_desc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        std::cerr << "No DRAM cores found on chip " << args.chip << std::endl;
        return 1;
    }
    CoreCoord core = dram_cores.front();

    std::cout << "Exporting TLB window: chip=" << args.chip << " core=(" << core.x << "," << core.y << ") addr=0x"
              << std::hex << args.addr << std::dec << " size=" << args.size << " ordering=" << args.ordering
              << std::endl;

    // export_dmabuf() takes the export length directly and rounds the underlying TLB window up to
    // the arch's next valid size class internally, so --size needs no arch-specific adjustment here.
    // It throws on every failure path (bad alignment, no window large enough, KMD too old), so the
    // returned fd is always valid; main()'s handler turns a throw into a clean "Fatal: ..." line.
    int dmabuf_fd = cluster->export_dmabuf(args.chip, core, args.addr, args.size, ordering);
    std::cout << "Got dma-buf fd " << dmabuf_fd << std::endl;

    // --- RDMA side: register the dma-buf as an MR --------------------------------------------
    RdmaPort rdma = open_active_rdma_port(args.dev, args.ib_port);
    if (rdma.is_roce()) {
        args.gid_index = resolve_gid_index(rdma.ctx, rdma.ib_port, args.gid_index);
    }

    ibv_pd* pd = ibv_alloc_pd(rdma.ctx);
    if (!pd) {
        std::cerr << "ibv_alloc_pd failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    // offset=0, iova=0: the dma-buf has no CPU-side virtual address, so remote_addr on the
    // initiator's WRITE/READ must also be 0 to match this MR's iova.
    //
    // REMOTE_READ is granted unconditionally alongside REMOTE_WRITE so one target invocation serves
    // either direction. Omitting it is the classic way an RDMA_READ dies with
    // IBV_WC_REM_ACCESS_ERR on the initiator while the write path looks perfectly healthy.
    ibv_mr* mr = ibv_reg_dmabuf_mr(
        pd, 0, args.size, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!mr) {
        perror("ibv_reg_dmabuf_mr failed");
        return 1;
    }

    ibv_cq* cq = ibv_create_cq(rdma.ctx, 1, nullptr, nullptr, 0);
    if (!cq) {
        std::cerr << "ibv_create_cq failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    // The QP must permit remote reads too, or an RDMA_READ is rejected at the QP level even though
    // the MR above allows it.
    ibv_qp* qp = create_rc_qp(pd, cq, rdma.ib_port, 1, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

    QpExchangeInfo local = make_local_exchange_info(rdma, args.gid_index, qp->qp_num, 0x1234);
    local.rkey = mr->rkey;
    local.remote_addr = 0;
    local.mr_length = args.size;

    // In read mode the initiator pulls *from* device memory, so the pattern has to already be there.
    // Seed it via UMD (a path independent of the exported window) before the handshake, so it is in
    // place before the peer can possibly issue a READ.
    if (is_read) {
        std::vector<uint8_t> seed(args.size);
        fill_pattern(seed.data(), seed.size());
        cluster->write_to_device(seed.data(), seed.size(), args.chip, core, args.addr);
        std::cout << "Seeded " << args.size << " bytes of test pattern into device memory via UMD" << std::endl;
    }

    // --- OOB handshake -------------------------------------------------------------------------
    std::cout << "Waiting for initiator on port " << args.port << "..." << std::endl;
    int conn = tcp_listen_accept(args.port);
    QpExchangeInfo remote{};
    send_all(conn, &local, sizeof(local));
    recv_all(conn, &remote, sizeof(remote));
    check_peer_exchange_info(remote);
    std::cout << "Peer QPN=" << remote.qpn << std::endl;

    connect_rc_qp(qp, rdma, args.gid_index, remote, local.psn);

    // Wait for the initiator to report the outcome of its RDMA batch (a real hardware ACK, not just
    // "posted"), so a failure on its side becomes a failure here too.
    BatchResult peer_result = BatchResult::Fail;
    recv_all(conn, &peer_result, 1);
    if (peer_result != BatchResult::Pass) {
        std::cerr << "Initiator reported FAIL for its RDMA batch" << std::endl;
    }

    bool all_match = (peer_result == BatchResult::Pass);
    if (is_read) {
        // The data moved device -> host, so the initiator holds it and verifies on its side; there is
        // nothing to check here. Device memory should be unchanged from what we seeded.
        std::cout << "Initiator signaled read complete; it verifies the pattern on its side." << std::endl;
    } else if (all_match) {
        std::cout << "Initiator signaled write complete; verifying via UMD readback..." << std::endl;

        // The initiator's CQE only proves its NIC got a RoCE-level ACK; it says nothing about the
        // resulting PCIe -> TLB -> NOC -> DRAM writes having landed. The readback below goes through a
        // *different* TLB window, and the exported window's default Relaxed ordering imposes nothing
        // against it, so without a barrier this races the tail of the RDMA batch and shows up as an
        // intermittent, unreproducible "Mismatch at byte N" that reads as a dma-buf bug.
        cluster->dram_membar(args.chip, std::unordered_set<CoreCoord>{core});

        std::vector<uint8_t> readback(args.size);
        cluster->read_from_device(readback.data(), args.chip, core, args.addr, args.size);

        // Full compare against a position-dependent pattern: a prefix-only check passes when just the
        // first page landed, and a byte-periodic pattern cannot detect a wrong-address bug at all.
        const size_t mismatch = find_pattern_mismatch(readback.data(), readback.size());
        if (mismatch != readback.size()) {
            std::cerr << "Mismatch at byte " << mismatch << ": expected " << static_cast<int>(pattern_byte(mismatch))
                      << " got " << static_cast<int>(readback[mismatch]) << std::endl;
            all_match = false;
        }
        std::cout << (all_match ? "PASS" : "FAIL") << std::endl;
    }

    close(conn);
    close(dmabuf_fd);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(rdma.ctx);
    return all_match ? 0 : 1;
}

int main(int argc, char** argv) {
    // recv_all()/send_all(), the RDMA bring-up helpers and export_dmabuf() all throw; catch here so a
    // peer or config failure prints a clean error instead of an uncaught-exception core dump.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
