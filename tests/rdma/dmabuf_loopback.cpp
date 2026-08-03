// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
//
// dmabuf_loopback - single-host RDMA loopback read benchmark: is a NIC-driven DMA read out of
// Blackhole DRAM faster than a CPU-driven (programmed-I/O) read through a mapped TLB window?
//
// Motivation: UMD's Cluster::read_from_device() ultimately does a CPU memcpy against an mmap'd
// BAR window (LocalChip::read_from_device -> TlbWindow::read_block). CPU loads against MMIO are
// non-posted and almost entirely latency-serialized - the core sustains very little
// memory-level parallelism there - which is why device->host reads are so much slower than
// host->device writes. An RDMA NIC, by contrast, is a DMA engine that keeps many non-posted PCIe
// reads in flight at once. This test measures both against the same DRAM tile, same host, same
// PCIe link, at the same transfer size, so the only variable is who drives the transactions.
//
// No second host and no network config are needed: both ends of the RC connection are QPs on the
// same port, cross-connected using the port's own GID, so the NIC loops the traffic back
// internally. The data still travels BH BAR -> PCIe -> NIC -> PCIe -> host DRAM, so the PCIe read
// path being measured is the real one.
//
// Flow:
//   1. Seed a known pattern into device DRAM with cluster->write_to_device().
//      This deliberately goes through a *different* TLB window (TlbManager's static/cached WC
//      window) than the dedicated window export_dmabuf() creates below.
//   2. Export a TLB window over the same DRAM tile as a dma-buf and register it as an RDMA MR.
//   3. RDMA READ the region into a host buffer over the loopback QP pair, timed.
//   4. Verify the full buffer against the seeded pattern.
//   5. Read the same region again via cluster->read_from_device() (the CPU/PIO path), timed.
//   6. Report both rates and the speedup.
//
// Because step 1 writes through one window and step 3 reads through another, a passing verify is
// independent evidence that the exported window really reaches device DRAM - it is not a
// self-consistent "write it and read it back through the same window" check.
//
// To build: part of the tt-umd CMake build behind TT_UMD_BUILD_RDMA_TESTS.
// To run:
//   ./dmabuf_loopback -r rocep201s0f0                  # defaults: chip 0, 64 MiB, 10 iters
//   ./dmabuf_loopback -r rocep201s0f0 -s 256 -i 20     # 256 MiB x 20 RDMA iterations
//   ./dmabuf_loopback -r rocep201s0f0 -p 0             # skip the slow CPU/PIO baseline

#include <fcntl.h>
#include <infiniband/verbs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

constexpr uint64_t MIB = 1ULL << 20;
constexpr enum ibv_mtu RDMA_MTU_MAX = IBV_MTU_4096;

// Golden-ratio multiplier: gives a distinct, non-trivial value per word index, so a partial or
// misaddressed transfer cannot accidentally verify.
constexpr uint64_t PATTERN_MULT = 0x9E3779B97F4A7C15ULL;

// Prefilled into the RDMA destination buffer before the read. Not a valid pattern word, so a read
// that silently moved nothing fails the verify instead of passing on stale data.
constexpr uint8_t SENTINEL = 0xAA;

struct Args {
    tt::ChipId chip = 0;
    const char* rdma_name = nullptr;
    int gid_index = -1;
    uint64_t addr = 0;
    size_t size = 64 * MIB;  // transfer size, used for BOTH the RDMA and PIO measurements
    size_t chunk = 4 * MIB;  // bytes per RDMA work request
    int outstanding = 16;    // RDMA reads kept in flight
    uint64_t iters = 10;     // RDMA read iterations
    uint64_t pio_iters = 1;  // CPU/PIO baseline iterations (slow; 0 skips it)
};

}  // namespace

#define DIE(fmt, ...)                                       \
    do {                                                    \
        fprintf(stderr, "error: " fmt "\n", ##__VA_ARGS__); \
        exit(1);                                            \
    } while (0)

/* ===== helpers ===== */

static double now_sec() {
    struct timespec ts {};

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Prints a labelled rate line and returns the rate, so the caller can compute a ratio.
static double report(const char* label, size_t bytes, double dt) {
    double gib_s = static_cast<double>(bytes) / dt / static_cast<double>(1ULL << 30);

    printf("%-30s %6zu MiB in %9.3f ms  (%7.2f GiB/s)\n", label, bytes >> 20, dt * 1e3, gib_s);
    return gib_s;
}

/* ===== GID selection ===== */

static int gid_is_rocev2(const char* rdma_name, uint8_t port, int index) {
    char path[256];
    char buf[64];

    snprintf(path, sizeof(path), "/sys/class/infiniband/%s/ports/%u/gid_attrs/types/%d", rdma_name, port, index);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    return strstr(buf, "RoCE v2") != nullptr;
}

static int pick_rocev2_gid(struct ibv_context* ctx, const char* rdma_name, uint8_t port) {
    static const uint8_t v4_mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

    union ibv_gid g {};

    for (int i = 0; i < 16; i++) {
        if (ibv_query_gid(ctx, port, i, &g)) {
            break;
        }
        if (memcmp(g.raw, v4_mapped_prefix, sizeof(v4_mapped_prefix)) != 0) {
            continue;
        }
        if (gid_is_rocev2(rdma_name, port, i)) {
            return i;
        }
    }
    return -1;
}

static struct ibv_context* open_rdma_device(const char* name) {
    struct ibv_device** list;
    struct ibv_context* ctx = nullptr;
    int n;

    list = ibv_get_device_list(&n);
    if (!list || n == 0) {
        DIE("no RDMA devices found");
    }
    for (int i = 0; i < n; i++) {
        if (!name || strcmp(ibv_get_device_name(list[i]), name) == 0) {
            ctx = ibv_open_device(list[i]);
            break;
        }
    }
    ibv_free_device_list(list);
    if (!ctx) {
        DIE("RDMA device %s not found/openable", name ? name : "(first)");
    }
    return ctx;
}

/* ===== loopback RC connection: two QPs on one port, cross-connected ===== */

struct lb {
    struct ibv_context* ctx = nullptr;
    struct ibv_pd* pd = nullptr;
    struct ibv_cq* cq = nullptr;
    struct ibv_qp* qp_req = nullptr;  // requester: issues the RDMA READs
    struct ibv_qp* qp_rsp = nullptr;  // responder: serves them out of the dma-buf MR
    uint8_t port = 1;
    int gid_index = -1;
    // These are two *different* device limits and must be clamped separately:
    //   init = how many RDMA reads this QP may have outstanding as requester (max_qp_init_rd_atom)
    //   dest = how many it may have inbound as responder            (max_qp_rd_atom)
    // They are frequently equal, but nothing guarantees it.
    int rd_atomic_init = 1;
    int rd_atomic_dest = 1;

    union ibv_gid gid {};
};

static struct ibv_qp* make_qp(struct lb* c, int sq_depth) {
    struct ibv_qp_init_attr ia {};

    ia.send_cq = c->cq;
    ia.recv_cq = c->cq;
    ia.qp_type = IBV_QPT_RC;
    ia.cap.max_send_wr = static_cast<uint32_t>(sq_depth);
    ia.cap.max_recv_wr = 4;
    ia.cap.max_send_sge = 1;
    ia.cap.max_recv_sge = 1;

    struct ibv_qp* qp = ibv_create_qp(c->pd, &ia);
    if (!qp) {
        DIE("ibv_create_qp failed: %s", strerror(errno));
    }

    struct ibv_qp_attr init {};

    init.qp_state = IBV_QPS_INIT;
    init.pkey_index = 0;
    init.port_num = c->port;
    // Both QPs get the full set: the responder needs REMOTE_READ to serve reads out of the
    // dma-buf MR, and granting it symmetrically keeps the two setup paths identical.
    init.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(qp, &init, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        DIE("modify INIT failed: %s", strerror(errno));
    }
    return qp;
}

// Move `qp` to RTR pointing at `dest_qpn`, then to RTS. The destination GID is our *own* GID -
// that is what makes this a loopback: the NIC recognises the target as local and turns the
// traffic around internally rather than putting it on the wire.
static void connect_qp(
    struct lb* c, struct ibv_qp* qp, uint32_t dest_qpn, uint32_t dest_psn, uint32_t local_psn, enum ibv_mtu mtu) {
    struct ibv_qp_attr rtr {};

    rtr.qp_state = IBV_QPS_RTR;
    rtr.path_mtu = mtu;
    rtr.dest_qp_num = dest_qpn;
    rtr.rq_psn = dest_psn;
    rtr.max_dest_rd_atomic = static_cast<uint8_t>(c->rd_atomic_dest);
    rtr.min_rnr_timer = 12;
    rtr.ah_attr.is_global = 1;
    rtr.ah_attr.port_num = c->port;
    rtr.ah_attr.grh.sgid_index = c->gid_index;
    rtr.ah_attr.grh.hop_limit = 64;
    memcpy(rtr.ah_attr.grh.dgid.raw, c->gid.raw, 16);

    if (ibv_modify_qp(
            qp,
            &rtr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER)) {
        DIE("modify RTR failed: %s (loopback unsupported on this port?)", strerror(errno));
    }

    struct ibv_qp_attr rts {};

    rts.qp_state = IBV_QPS_RTS;
    rts.timeout = 14;
    rts.retry_cnt = 7;
    rts.rnr_retry = 7;
    rts.sq_psn = local_psn;
    rts.max_rd_atomic = static_cast<uint8_t>(c->rd_atomic_init);
    if (ibv_modify_qp(
            qp,
            &rts,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC)) {
        DIE("modify RTS failed: %s", strerror(errno));
    }
}

static void setup_loopback(struct lb* c, const Args& args) {
    c->port = 1;
    c->ctx = open_rdma_device(args.rdma_name);
    c->pd = ibv_alloc_pd(c->ctx);
    if (!c->pd) {
        DIE("ibv_alloc_pd failed");
    }

    // Sized for both QPs' completions even though only the requester generates any (RDMA READ is
    // one-sided: the responder produces no CQE).
    c->cq = ibv_create_cq(c->ctx, args.outstanding * 2 + 16, nullptr, nullptr, 0);
    if (!c->cq) {
        DIE("ibv_create_cq failed");
    }

    c->gid_index = args.gid_index;
    if (c->gid_index < 0) {
        c->gid_index = pick_rocev2_gid(c->ctx, args.rdma_name, c->port);
        if (c->gid_index < 0) {
            DIE("no RoCEv2 (IPv4-mapped) GID found; pass -g");
        }
    }
    if (ibv_query_gid(c->ctx, c->port, c->gid_index, &c->gid)) {
        DIE("ibv_query_gid failed");
    }

    // Outstanding RDMA READs are capped by the QP's max_rd_atomic, which in turn cannot exceed
    // what the HCA reports. Clamp rather than silently under-delivering the requested depth.
    struct ibv_device_attr dev_attr {};

    int hw_init = 16;
    int hw_dest = 16;
    if (ibv_query_device(c->ctx, &dev_attr) == 0) {
        if (dev_attr.max_qp_init_rd_atom > 0) {
            hw_init = dev_attr.max_qp_init_rd_atom;
        }
        if (dev_attr.max_qp_rd_atom > 0) {
            hw_dest = dev_attr.max_qp_rd_atom;
        }
    }
    // Both attributes are capped at 255 by the uint8_t fields in ibv_qp_attr, so a device
    // reporting more than that cannot actually be asked for more.
    c->rd_atomic_init = std::min({args.outstanding, hw_init, 255});
    c->rd_atomic_dest = std::min({args.outstanding, hw_dest, 255});
    if (c->rd_atomic_init < args.outstanding) {
        printf(
            "note: clamping reads in flight %d -> %d (HCA max_qp_init_rd_atom=%d)\n",
            args.outstanding,
            c->rd_atomic_init,
            hw_init);
    }

    enum ibv_mtu mtu = RDMA_MTU_MAX;

    struct ibv_port_attr pa {};

    if (!ibv_query_port(c->ctx, c->port, &pa)) {
        if (pa.state != IBV_PORT_ACTIVE) {
            DIE("port 1 of %s is not ACTIVE", args.rdma_name ? args.rdma_name : "(first)");
        }
        mtu = std::min(pa.active_mtu, mtu);
    }

    const int sq_depth = args.outstanding + 8;
    c->qp_req = make_qp(c, sq_depth);
    c->qp_rsp = make_qp(c, sq_depth);

    const uint32_t psn_req = 0x111111;
    const uint32_t psn_rsp = 0x222222;
    connect_qp(c, c->qp_req, c->qp_rsp->qp_num, psn_rsp, psn_req, mtu);
    connect_qp(c, c->qp_rsp, c->qp_req->qp_num, psn_req, psn_rsp, mtu);

    printf(
        "loopback RC up: qpn %u <-> %u, gid_index=%d, path MTU=%d, %d reads in flight\n",
        c->qp_req->qp_num,
        c->qp_rsp->qp_num,
        c->gid_index,
        128 << mtu,
        c->rd_atomic_init);
}

// Chunked, pipelined RDMA READ over [0, total) into host memory at `host_base`. The dma-buf MR is
// registered with iova 0, so remote_addr is just the byte offset.
static void rdma_read(
    struct lb* c,
    struct ibv_mr* local_mr,
    uintptr_t host_base,
    size_t total,
    uint64_t remote_base,
    uint32_t rkey,
    size_t chunk,
    int outstanding) {
    size_t off = 0;
    int inflight = 0;

    while (off < total || inflight) {
        while (off < total && inflight < outstanding) {
            size_t len = std::min(total - off, chunk);

            struct ibv_sge sge {};

            sge.addr = static_cast<uint64_t>(host_base + off);
            sge.length = static_cast<uint32_t>(len);
            sge.lkey = local_mr->lkey;

            struct ibv_send_wr wr {};

            wr.wr_id = off;
            wr.sg_list = &sge;
            wr.num_sge = 1;
            wr.opcode = IBV_WR_RDMA_READ;
            wr.send_flags = IBV_SEND_SIGNALED;
            wr.wr.rdma.remote_addr = remote_base + off;
            wr.wr.rdma.rkey = rkey;

            struct ibv_send_wr* bad = nullptr;
            if (ibv_post_send(c->qp_req, &wr, &bad)) {
                DIE("ibv_post_send failed at off=%zu: %s", off, strerror(errno));
            }
            off += len;
            inflight++;
        }

        struct ibv_wc wc {};

        int n = ibv_poll_cq(c->cq, 1, &wc);
        if (n < 0) {
            DIE("ibv_poll_cq failed");
        }
        if (n > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                DIE("completion error: %s (wr off=%llu)",
                    ibv_wc_status_str(wc.status),
                    static_cast<unsigned long long>(wc.wr_id));
            }
            inflight--;
        }
    }
}

static void usage(const char* argv0) {
    printf(
        "usage: %s -r rdma_dev [options]\n"
        "  -c N   chip id (default 0)\n"
        "  -r S   RDMA device name, e.g. rocep201s0f0\n"
        "  -g N   GID index (default: auto-detect RoCEv2 IPv4-mapped)\n"
        "  -a N   DRAM address (default 0, must be page-aligned)\n"
        "  -s N   transfer size in MiB, used for both measurements (default 64)\n"
        "  -k N   RDMA chunk size in MiB per work request (default 4)\n"
        "  -o N   RDMA reads in flight (default 16)\n"
        "  -i N   RDMA read iterations (default 10)\n"
        "  -p N   CPU/PIO baseline iterations (default 1, 0 to skip)\n",
        argv0);
}

int run(int argc, char** argv) {
    Args args;
    int opt;

    while ((opt = getopt(argc, argv, "c:r:g:a:s:k:o:i:p:h")) != -1) {
        switch (opt) {
            case 'c':
                args.chip = static_cast<tt::ChipId>(atoi(optarg));
                break;
            case 'r':
                args.rdma_name = optarg;
                break;
            case 'g':
                args.gid_index = atoi(optarg);
                break;
            case 'a':
                args.addr = strtoull(optarg, nullptr, 0);
                break;
            case 's':
                args.size = static_cast<size_t>(strtoull(optarg, nullptr, 0) * MIB);
                break;
            case 'k':
                args.chunk = static_cast<size_t>(strtoull(optarg, nullptr, 0) * MIB);
                break;
            case 'o':
                args.outstanding = atoi(optarg);
                break;
            case 'i':
                args.iters = strtoull(optarg, nullptr, 0);
                break;
            case 'p':
                args.pio_iters = strtoull(optarg, nullptr, 0);
                break;
            default:
                usage(argv[0]);
                return opt == 'h' ? 0 : 2;
        }
    }

    if (args.size == 0 || args.chunk == 0 || args.outstanding < 1 || args.iters == 0) {
        DIE("size/chunk/outstanding/iters must all be non-zero (see -h)");
    }
    if (!args.rdma_name) {
        printf("note: no -r given, using the first RDMA device found\n");
    }

    const size_t nwords = args.size / sizeof(uint64_t);

    // --- UMD side: seed the DRAM tile through a window that is NOT the exported one ------------
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->get_tt_device(args.chip)->set_power_state(true);

    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(args.chip);
    std::vector<CoreCoord> dram_cores = soc_desc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        DIE("no DRAM cores found on chip %d", static_cast<int>(args.chip));
    }
    CoreCoord core = dram_cores.front();

    std::vector<uint64_t> pattern(nwords);
    for (size_t j = 0; j < nwords; j++) {
        pattern[j] = static_cast<uint64_t>(j) * PATTERN_MULT;
    }

    printf(
        "chip %d core=(x=%zu,y=%zu) addr=0x%" PRIx64 " size=%zu MiB\n",
        static_cast<int>(args.chip),
        core.x,
        core.y,
        args.addr,
        args.size / MIB);

    // write_to_device() lands on TlbManager's static/cached WC window - a completely different
    // TLB window from the dedicated one export_dmabuf() allocates below. That independence is
    // what makes the later verify meaningful rather than self-referential.
    double t0 = now_sec();
    cluster->write_to_device(pattern.data(), args.size, args.chip, core, args.addr);
    double t1 = now_sec();
    report("UMD seed write (PIO)", args.size, t1 - t0);

    // --- export the same tile as a dma-buf and register it -------------------------------------
    int dmabuf_fd = cluster->export_dmabuf(args.chip, core, args.addr, args.size);

    struct lb c;
    setup_loopback(&c, args);

    // Read concurrency is bounded by the number of work requests in the transfer, not by -o alone:
    // at the defaults (-s 64 -k 4) the whole transfer is only 16 chunks, so a larger -o has nothing
    // to fill it with. Say so rather than silently under-delivering the requested depth.
    const size_t nchunks = (args.size + args.chunk - 1) / args.chunk;
    if (nchunks < static_cast<size_t>(c.rd_atomic_init)) {
        printf(
            "note: -s %zu / -k %zu is only %zu chunks, so at most %zu of the %d reads in flight are"
            " used (lower -k or raise -s to go deeper)\n",
            args.size / MIB,
            args.chunk / MIB,
            nchunks,
            nchunks,
            c.rd_atomic_init);
    }

    // iova 0, so remote_addr on the READ is a plain byte offset. REMOTE_READ is the flag that
    // matters here; without it the read dies with IBV_WC_REM_ACCESS_ERR.
    struct ibv_mr* dev_mr = ibv_reg_dmabuf_mr(
        c.pd, 0, args.size, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!dev_mr) {
        DIE("ibv_reg_dmabuf_mr failed: %s (kmd >= 2.10.0-rc1 and kernel >= 5.8 required)", strerror(errno));
    }

    std::vector<uint64_t> host_buf(nwords);
    std::fill(
        reinterpret_cast<uint8_t*>(host_buf.data()), reinterpret_cast<uint8_t*>(host_buf.data()) + args.size, SENTINEL);
    struct ibv_mr* host_mr = ibv_reg_mr(c.pd, host_buf.data(), args.size, IBV_ACCESS_LOCAL_WRITE);
    if (!host_mr) {
        DIE("ibv_reg_mr failed: %s (raise `ulimit -l` to at least %zu MiB)", strerror(errno), args.size / MIB);
    }

    // --- measurement 1: NIC DMA read, device -> host over the loopback QP pair -----------------
    printf("\n");
    t0 = now_sec();
    for (uint64_t it = 0; it < args.iters; it++) {
        rdma_read(
            &c,
            host_mr,
            reinterpret_cast<uintptr_t>(host_buf.data()),
            args.size,
            0,
            dev_mr->rkey,
            args.chunk,
            c.rd_atomic_init);
    }
    t1 = now_sec();
    double rdma_rate = report("RDMA read  (dev->host, DMA)", args.size * args.iters, t1 - t0);

    // Full compare, not a prefix: the pattern is index-derived, so this also catches a transfer
    // that landed at the wrong offset.
    for (size_t j = 0; j < nwords; j++) {
        if (host_buf[j] != pattern[j]) {
            DIE("RDMA verify FAILED at word %zu: got=0x%016" PRIx64 " want=0x%016" PRIx64, j, host_buf[j], pattern[j]);
        }
    }
    printf("RDMA read verified against the UMD-seeded pattern (independent window)\n");

    // --- measurement 2: CPU/PIO baseline through UMD's mapped TLB window -----------------------
    double pio_rate = 0.0;
    if (args.pio_iters > 0) {
        std::vector<uint64_t> pio_buf(nwords, 0);

        t0 = now_sec();
        for (uint64_t it = 0; it < args.pio_iters; it++) {
            cluster->read_from_device(pio_buf.data(), args.chip, core, args.addr, args.size);
        }
        t1 = now_sec();
        pio_rate = report("UMD  read  (dev->host, PIO)", args.size * args.pio_iters, t1 - t0);

        for (size_t j = 0; j < nwords; j++) {
            if (pio_buf[j] != pattern[j]) {
                DIE("PIO verify FAILED at word %zu: got=0x%016" PRIx64 " want=0x%016" PRIx64,
                    j,
                    pio_buf[j],
                    pattern[j]);
            }
        }
    }

    if (pio_rate > 0.0) {
        printf("\nRDMA/PIO read speedup: %.2fx\n", rdma_rate / pio_rate);
    }
    printf("PASS\n");

    ibv_dereg_mr(host_mr);
    ibv_dereg_mr(dev_mr);
    close(dmabuf_fd);
    ibv_destroy_qp(c.qp_req);
    ibv_destroy_qp(c.qp_rsp);
    ibv_destroy_cq(c.cq);
    ibv_dealloc_pd(c.pd);
    ibv_close_device(c.ctx);
    return 0;
}

int main(int argc, char** argv) {
    // Cluster construction and export_dmabuf() throw (e.g. when the kmd is too old for
    // EXPORT_TLB_DMABUF); catch so that prints a clean error instead of an uncaught-exception abort.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }
}
