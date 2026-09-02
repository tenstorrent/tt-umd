// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// RDMA initiator for the two-host dma-buf bandwidth test. Registers a normal host-memory MR filled
// with a known test pattern, brings up an RC QP against the target, and issues repeated RDMA WRITEs
// into the target's dma-buf-backed MR (see dmabuf_target.cpp) to measure sustained write bandwidth.
// No UMD dependency here at all.
#include <infiniband/verbs.h>

#include <algorithm>
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
    // Must match dmabuf_target's --size, which defaults to the same value for the reason documented
    // there (2 MiB is the smaller of Blackhole's two TLB window size classes).
    uint64_t size = 2ull << 20;
    uint64_t iters = 100;      // number of RDMA ops of --size bytes to issue, timed as one batch
    std::string dev;           // empty = first RDMA device with an ACTIVE port
    int ib_port = -1;          // -1 = first ACTIVE port on that device
    int gid_index = -1;        // -1 = auto-detect a RoCEv2 GID (see resolve_gid_index in rdma_common.hpp)
    std::string op = "write";  // "write" = host -> device NOC; "read" = device NOC -> host
};

void print_usage() {
    std::cerr << "Usage: dmabuf_initiator --host <target-host> [--port N] [--size N] [--iters N]\n"
              << "                        [--dev NAME] [--ib-port N] [--gid-index N] [--op write|read]" << std::endl;
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host") {
            a.host = arg_value(argc, argv, i);
        } else if (arg == "--port") {
            a.port = static_cast<uint16_t>(std::stoi(arg_value(argc, argv, i)));
        } else if (arg == "--size") {
            a.size = std::stoull(arg_value(argc, argv, i));
        } else if (arg == "--iters") {
            a.iters = std::stoull(arg_value(argc, argv, i));
        } else if (arg == "--dev") {
            a.dev = arg_value(argc, argv, i);
        } else if (arg == "--ib-port") {
            a.ib_port = std::stoi(arg_value(argc, argv, i));
        } else if (arg == "--gid-index") {
            a.gid_index = std::stoi(arg_value(argc, argv, i));
        } else if (arg == "--op") {
            a.op = arg_value(argc, argv, i);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument '" + arg + "'");
        }
    }
    return a;
}

// Number of in-flight, unsignaled RDMA_WRITEs between each polled completion. RC QPs complete
// WQEs in order, so polling one CQE every SIGNAL_INTERVAL writes still confirms all of them landed.
// Kept at 1 (fully synchronous, one WRITE in flight at a time): at these transfer sizes ibv_poll_cq
// overhead is negligible next to per-message transfer time, so there's nothing to gain from batching
// completions — and batching here is actively dangerous, since going over 32 previously left ~2 GiB
// of unpaced, unacknowledged WRITEs outstanding and blew through the QP's retry budget ("transport
// retry counter exceeded") on a real fabric. Only raise this if you've confirmed the fabric tolerates
// more bytes in flight for your chosen --size.
constexpr uint64_t SIGNAL_INTERVAL = 1;

}  // namespace

int run(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    if (args.host.empty()) {
        print_usage();
        return 1;
    }
    if (args.op != "write" && args.op != "read") {
        std::cerr << "--op must be 'write' or 'read', got '" << args.op << "'" << std::endl;
        return 1;
    }
    const bool is_read = (args.op == "read");

    // Validate --size before allocating anything for it. One work request carries one SGE, whose
    // length is a uint32_t, while --size is a uint64_t: --size 4294967296 (4 GiB, a natural choice on
    // Blackhole) truncates sge.length to 0, so every WQE moves nothing, every CQE reports success,
    // and the bandwidth line below — computed from --size x --iters — reports a fabricated number as
    // a pass.
    if (args.size == 0) {
        std::cerr << "--size must be non-zero" << std::endl;
        return 1;
    }
    if (args.size > UINT32_MAX) {
        std::cerr << "--size " << args.size << " exceeds the " << UINT32_MAX
                  << " byte limit of a single RDMA work request; split the transfer or use a smaller --size"
                  << std::endl;
        return 1;
    }

    std::vector<uint8_t> local_buf(args.size);
    if (is_read) {
        // RDMA_READ pulls device memory into this buffer, so prefill with a sentinel that is NOT the
        // expected pattern — otherwise a read that silently moved nothing would still "verify".
        // The target seeds the real pattern into device memory via UMD before the handshake.
        std::fill(local_buf.begin(), local_buf.end(), 0xAA);
    } else {
        // Position-dependent pattern, so the target can verify without needing the buffer contents
        // out of band, and a misaddressed or partial transfer cannot accidentally match.
        fill_pattern(local_buf.data(), local_buf.size());
    }

    RdmaPort rdma = open_active_rdma_port(args.dev, args.ib_port);
    if (rdma.is_roce()) {
        args.gid_index = resolve_gid_index(rdma.ctx, rdma.ib_port, args.gid_index);
    }

    // port_attr.max_msg_sz is the device's own per-message ceiling (commonly 1 GiB), checked for the
    // same reason as the uint32_t limit above; it needs an opened port, so it lands here.
    if (args.size > rdma.port_attr.max_msg_sz) {
        std::cerr << "--size " << args.size << " exceeds the port's max_msg_sz of " << rdma.port_attr.max_msg_sz
                  << " bytes" << std::endl;
        return 1;
    }

    ibv_pd* pd = ibv_alloc_pd(rdma.ctx);
    if (!pd) {
        std::cerr << "ibv_alloc_pd failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    ibv_mr* mr = ibv_reg_mr(pd, local_buf.data(), local_buf.size(), IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        perror("ibv_reg_mr failed");
        return 1;
    }

    ibv_cq* cq = ibv_create_cq(rdma.ctx, 1, nullptr, nullptr, 0);
    if (!cq) {
        std::cerr << "ibv_create_cq failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    // Must hold at least SIGNAL_INTERVAL WQEs, since that many go unsignaled (and thus unretired)
    // between polls. No remote access is served on this side, so the QP needs no access flags.
    ibv_qp* qp = create_rc_qp(pd, cq, rdma.ib_port, static_cast<uint32_t>(SIGNAL_INTERVAL), 0);

    QpExchangeInfo local = make_local_exchange_info(rdma, args.gid_index, qp->qp_num, 0x5678);

    std::cout << "Connecting to " << args.host << ":" << args.port << "..." << std::endl;
    int conn = tcp_connect(args.host, args.port);
    QpExchangeInfo remote{};
    recv_all(conn, &remote, sizeof(remote));
    send_all(conn, &local, sizeof(local));
    check_peer_exchange_info(remote);
    std::cout << "Target QPN=" << remote.qpn << " rkey=" << remote.rkey << " mr_length=" << remote.mr_length
              << std::endl;

    // The two binaries are launched independently with their own --size. A larger local size overruns
    // the target's MR and dies with IBV_WC_REM_ACCESS_ERR at iteration 0, indistinguishable from a
    // permissions or dma-buf failure; a smaller one succeeds and prints a bandwidth figure for a
    // window that was never fully written.
    if (remote.mr_length != args.size) {
        std::cerr << "Size mismatch: this side has --size " << args.size << " but the target exported "
                  << remote.mr_length << " bytes. Launch both with the same --size." << std::endl;
        return 1;
    }

    connect_rc_qp(qp, rdma, args.gid_index, remote, local.psn);

    // Repeatedly move --size bytes between local_buf and the target's dma-buf MR at iova 0 (see
    // dmabuf_target.cpp; the dma-buf MR was registered with iova=0, so remote_addr here must also be
    // 0). Every iteration touches the same remote window, which is fine for a bandwidth measurement:
    // in write mode each iteration reproduces the identical pattern, and in read mode the source
    // device memory is never modified.
    //
    // Direction note: WRITE pushes host -> device NOC and is "posted" on the target's PCIe link
    // (fire-and-forget, pipelines well). READ pulls device NOC -> host, which makes the target NIC
    // issue *non-posted* PCIe reads against the card's BAR — each needs a completion to come back, so
    // throughput is bounded by read-completion concurrency and latency rather than raw link
    // bandwidth. Expect READ to be substantially slower than WRITE on the same link.
    ibv_sge sge{};
    sge.addr = reinterpret_cast<uint64_t>(local_buf.data());
    sge.length = static_cast<uint32_t>(local_buf.size());
    sge.lkey = mr->lkey;

    ibv_send_wr wr{};
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = is_read ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
    wr.wr.rdma.remote_addr = remote.remote_addr;
    wr.wr.rdma.rkey = remote.rkey;

    const char* op_name = is_read ? "READ" : "WRITE";
    std::cout << "Issuing " << args.iters << " x " << args.size << " byte RDMA " << op_name << "s..." << std::endl;

    bool batch_ok = true;
    auto t_start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < args.iters && batch_ok; ++i) {
        bool signal = ((i + 1) % SIGNAL_INTERVAL == 0) || (i + 1 == args.iters);
        wr.wr_id = i;
        wr.send_flags = signal ? IBV_SEND_SIGNALED : 0;

        ibv_send_wr* bad_wr = nullptr;
        if (ibv_post_send(qp, &wr, &bad_wr)) {
            std::cerr << "ibv_post_send failed at iter " << i << ": " << std::strerror(errno) << std::endl;
            batch_ok = false;
            break;
        }

        if (signal) {
            ibv_wc wc{};
            int n = 0;
            while (n == 0) {
                n = ibv_poll_cq(cq, 1, &wc);
                if (n < 0) {
                    std::cerr << "ibv_poll_cq failed" << std::endl;
                    batch_ok = false;
                    break;
                }
            }
            if (batch_ok && wc.status != IBV_WC_SUCCESS) {
                // wc.wr_id, not the loop counter i: once a fatal transport error hits, the QP flushes
                // all outstanding WQEs, so the CQE we get back may belong to an earlier, still-unsignaled
                // op rather than the one posted this iteration.
                std::cerr << "RDMA " << op_name << " failed, wr_id=" << wc.wr_id << ": " << ibv_wc_status_str(wc.status)
                          << std::endl;
                batch_ok = false;
            }
        }
    }
    auto t_end = std::chrono::steady_clock::now();

    if (batch_ok) {
        double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
        double total_bytes = static_cast<double>(args.size) * static_cast<double>(args.iters);
        double gbps = total_bytes / elapsed_s / 1e9;
        std::cout << (is_read ? "Read " : "Wrote ") << static_cast<uint64_t>(total_bytes) << " bytes in " << elapsed_s
                  << " s => " << gbps << " GB/s" << std::endl;
    }

    // In read mode the data landed on *this* side, so verification happens here rather than on the
    // target: check that the pattern the target seeded into device memory actually arrived, and that
    // we're not just looking at the 0xAA sentinel. The compare covers the whole buffer, since a
    // prefix-only check passes when only the first page was transferred.
    bool all_match = batch_ok;
    if (batch_ok && is_read) {
        const size_t mismatch = find_pattern_mismatch(local_buf.data(), local_buf.size());
        if (mismatch != local_buf.size()) {
            std::cerr << "Mismatch at byte " << mismatch << ": expected " << static_cast<int>(pattern_byte(mismatch))
                      << " got " << static_cast<int>(local_buf[mismatch]) << std::endl;
            all_match = false;
        }
        std::cout << (all_match ? "PASS" : "FAIL") << std::endl;
    }

    // Report the outcome to the target rather than an unconditional "done": it owns the device and is
    // the natural side for a CI harness to watch, so a failure here has to reach its exit status too.
    BatchResult result = all_match ? BatchResult::Pass : BatchResult::Fail;
    send_all(conn, &result, 1);

    close(conn);
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(rdma.ctx);
    return all_match ? 0 : 1;
}

int main(int argc, char** argv) {
    // tcp_connect()/send_all()/recv_all(), the RDMA bring-up helpers and resolve_gid_index() all
    // throw; catch here so a handshake or config failure prints a clean error instead of an
    // uncaught-exception core dump.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
