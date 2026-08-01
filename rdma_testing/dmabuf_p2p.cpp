// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
//
// dmabuf_p2p - minimal NIC <-> Blackhole peer-to-peer DMA demo, ported from tt-kmd's
// tools/rdma/nic_bh_p2p_dma.c to go through UMD's Cluster API instead of raw ioctls against
// /dev/tenstorrent/N. Same flow, same wire protocol, same RDMA plumbing - the only thing that
// changes is how the Blackhole side aims a TLB window and exports it as a dma-buf:
// Cluster::export_dmabuf() in place of ALLOCATE_TLB + CONFIGURE_TLB + EXPORT_TLB_DMABUF.
//
// Demonstrates that an RDMA NIC can DMA directly into and out of Blackhole GDDR over the fabric,
// with both transfers initiated by the NIC and no Blackhole CPU involvement on the data path.
//
// Single binary, two roles (traffic flows over the wire between the two NICs):
//   server (no peer arg): the Blackhole node. Exports a DRAM tile as an MR and is otherwise
//     passive - it never touches the data.
//   client (peer arg = server IP): the NIC that drives the DMA:
//     1. RDMA WRITE a pattern into BH   (NIC -> BH)
//     2. RDMA READ  it back out of BH   (BH -> NIC)
//     3. verify the read-back matches what was written.
//
// The DRAM tile and transfer size are compile-time constants (as in the reference); the RDMA
// device name and chip id are command-line arguments.
//
// To build: part of the tt-umd CMake build behind TT_UMD_BUILD_RDMA_TESTS (see
// rdma_testing/README.md for prerequisites).
// To run (RoCE example, gid index 3 = RoCEv2 IPv4):
//   # on the Blackhole node:
//   ./dmabuf_p2p -c 0 -r rocep201s0f0 -g 3
//   # on the peer node:
//   ./dmabuf_p2p -r rocep201s0f0 -g 3 10.32.34.129

#include <arpa/inet.h>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/socket.h>
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

/* ===== hard-coded test parameters (mirrors nic_bh_p2p_dma.c) ===== */

#define NOC_ADDR 0ULL             // DRAM tile base address (must be page-aligned)
#define XFER_SIZE (256ULL << 20)  // bytes written/read/verified (256 MiB)

#define RDMA_MTU_MAX IBV_MTU_4096

#define RDMA_CHUNK (64ULL << 20)  // 64 MiB per work request
#define OUTSTANDING 4             // in-flight work requests
#define SQ_DEPTH (OUTSTANDING + 8)
#define MAX_RD_ATOMIC 16  // outstanding RDMA reads the QP allows

#define TCP_PORT 18515

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

static void report(const char* label, size_t bytes, double dt) {
    printf(
        "%-16s %5zu MiB in %8.3f ms  (%6.2f GiB/s)\n",
        label,
        bytes >> 20,
        dt * 1e3,
        static_cast<double>(bytes) / dt / static_cast<double>(1ULL << 30));
}

static void fill_random(void* buf, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(buf);

    while (n) {
        ssize_t r = getrandom(p, n, 0);

        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            DIE("getrandom failed: %s", strerror(errno));
        }
        p += r;
        n -= static_cast<size_t>(r);
    }
}

/* ===== RDMA plumbing (verbatim flow from nic_bh_p2p_dma.c) ===== */

struct conn {
    struct ibv_context* ctx;
    struct ibv_pd* pd;
    struct ibv_cq* cq;
    struct ibv_qp* qp;
    uint8_t port;
    int gid_index;
};

// Wire format for the out-of-band TCP handshake. Both nodes are assumed to share host byte order
// (x86-64 / aarch64 little-endian) for simplicity.
struct exch {
    uint32_t rkey;  // server: bh_mr rkey
    uint32_t qpn;
    uint32_t psn;
    uint64_t addr;  // server: bh_mr iova (0)
    uint8_t gid[16];
};

// Read the GID type string for a given index, e.g. "RoCE v2" or "IB/RoCE v1", from sysfs.
// ibv_query_gid() does not expose the RoCE version, so a port that has both v1 and v2 GIDs for
// the same IPv4 address looks identical without it.
static int gid_is_rocev2(const char* rdma_name, uint8_t port, int index) {
    char path[256];
    char buf[64];
    ssize_t n;
    int fd;

    snprintf(path, sizeof(path), "/sys/class/infiniband/%s/ports/%u/gid_attrs/types/%d", rdma_name, port, index);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    return strstr(buf, "RoCE v2") != nullptr;
}

// Auto-select a RoCEv2 (IPv4-mapped) GID. We must check both the GID value (IPv4-mapped prefix)
// and the GID type, since many ports expose a RoCE v1 and a RoCE v2 GID for the same address -
// picking the v1 one fails to connect.
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

static void setup_verbs(struct conn* c, const char* rdma_name, int gid_index, struct exch* local) {
    struct ibv_qp_init_attr ia {};

    union ibv_gid gid {};

    c->port = 1;
    c->ctx = open_rdma_device(rdma_name);
    c->pd = ibv_alloc_pd(c->ctx);
    if (!c->pd) {
        DIE("ibv_alloc_pd failed");
    }

    c->cq = ibv_create_cq(c->ctx, OUTSTANDING + 16, nullptr, nullptr, 0);
    if (!c->cq) {
        DIE("ibv_create_cq failed");
    }

    if (gid_index < 0) {
        gid_index = pick_rocev2_gid(c->ctx, rdma_name, c->port);
        if (gid_index < 0) {
            DIE("no RoCEv2 (IPv4-mapped) GID found; pass -g");
        }
    }
    c->gid_index = gid_index;

    if (ibv_query_gid(c->ctx, c->port, c->gid_index, &gid)) {
        DIE("ibv_query_gid failed");
    }

    ia.cap.max_send_wr = SQ_DEPTH;
    ia.cap.max_recv_wr = 4;
    ia.cap.max_send_sge = 1;
    ia.cap.max_recv_sge = 1;
    ia.qp_type = IBV_QPT_RC;
    ia.send_cq = c->cq;
    ia.recv_cq = c->cq;
    c->qp = ibv_create_qp(c->pd, &ia);
    if (!c->qp) {
        DIE("ibv_create_qp failed: %s", strerror(errno));
    }

    memcpy(local->gid, gid.raw, 16);
    local->qpn = c->qp->qp_num;
    local->psn = lrand48() & 0xffffff;
    printf("local:  qpn=0x%x gid_index=%d\n", local->qpn, c->gid_index);
}

static void connect_qp(struct conn* c, const struct exch* local, const struct exch* remote) {
    enum ibv_mtu mtu = RDMA_MTU_MAX;

    struct ibv_port_attr pa {};

    struct ibv_qp_attr init {};

    struct ibv_qp_attr rtr {};

    struct ibv_qp_attr rts {};

    init.qp_state = IBV_QPS_INIT;
    init.pkey_index = 0;
    init.port_num = c->port;
    init.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    rtr.qp_state = IBV_QPS_RTR;
    rtr.dest_qp_num = remote->qpn;
    rtr.rq_psn = remote->psn;
    rtr.max_dest_rd_atomic = MAX_RD_ATOMIC;
    rtr.min_rnr_timer = 12;
    rtr.ah_attr.is_global = 1;
    rtr.ah_attr.port_num = c->port;
    rtr.ah_attr.grh.sgid_index = c->gid_index;
    rtr.ah_attr.grh.hop_limit = 64;

    rts.qp_state = IBV_QPS_RTS;
    rts.timeout = 14;
    rts.retry_cnt = 7;
    rts.rnr_retry = 7;
    rts.sq_psn = local->psn;
    rts.max_rd_atomic = MAX_RD_ATOMIC;

    // Clamp the RoCE MTU to what the port negotiated, so we never emit packets larger than the
    // Ethernet link can carry (which would just be dropped -> "transport retry counter exceeded").
    if (!ibv_query_port(c->ctx, c->port, &pa) && pa.active_mtu < mtu) {
        mtu = pa.active_mtu;
    }
    rtr.path_mtu = mtu;
    printf("path MTU = %d bytes\n", 128 << mtu);

    memcpy(rtr.ah_attr.grh.dgid.raw, remote->gid, 16);

    if (ibv_modify_qp(c->qp, &init, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        DIE("modify INIT failed: %s", strerror(errno));
    }
    if (ibv_modify_qp(
            c->qp,
            &rtr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER)) {
        DIE("modify RTR failed: %s (gid/route issue?)", strerror(errno));
    }
    if (ibv_modify_qp(
            c->qp,
            &rts,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC)) {
        DIE("modify RTS failed: %s", strerror(errno));
    }
}

// Chunked, pipelined RDMA over [0, total). The remote iova base is 0, so remote_addr == byte
// offset. Up to OUTSTANDING work requests are kept in flight.
static void rdma_xfer(
    struct conn* c,
    enum ibv_wr_opcode op,
    struct ibv_mr* mr,
    const uint8_t* host,
    size_t total,
    uint64_t remote_base,
    uint32_t rkey) {
    size_t off = 0;
    int inflight = 0;

    while (off < total || inflight) {
        while (off < total && inflight < OUTSTANDING) {
            size_t len = (total - off < RDMA_CHUNK) ? total - off : RDMA_CHUNK;

            struct ibv_sge sge {};

            sge.addr = reinterpret_cast<uintptr_t>(host + off);
            sge.length = static_cast<uint32_t>(len);
            sge.lkey = mr->lkey;

            struct ibv_send_wr wr {};

            wr.wr_id = off;
            wr.sg_list = &sge;
            wr.num_sge = 1;
            wr.opcode = op;
            wr.send_flags = IBV_SEND_SIGNALED;
            wr.wr.rdma.remote_addr = remote_base + off;
            wr.wr.rdma.rkey = rkey;

            struct ibv_send_wr* bad = nullptr;

            if (ibv_post_send(c->qp, &wr, &bad)) {
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

/* ===== TCP out-of-band exchange ===== */

static void rw_all(int fd, void* buf, size_t n, int writing) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    while (n) {
        ssize_t r = writing ? write(fd, p, n) : read(fd, p, n);
        if (r <= 0) {
            DIE("tcp %s failed: %s", writing ? "write" : "read", strerror(errno));
        }
        p += r;
        n -= static_cast<size_t>(r);
    }
}

static int tcp_listen_accept() {
    struct sockaddr_in addr {};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int lfd;
    int fd;
    int yes = 1;

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        DIE("socket: %s", strerror(errno));
    }
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(lfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
        DIE("bind: %s", strerror(errno));
    }
    if (listen(lfd, 1)) {
        DIE("listen: %s", strerror(errno));
    }
    printf("waiting for client on tcp/%d ...\n", TCP_PORT);
    fd = accept(lfd, nullptr, nullptr);
    if (fd < 0) {
        DIE("accept: %s", strerror(errno));
    }
    close(lfd);
    return fd;
}

static int tcp_connect(const char* host) {
    struct sockaddr_in addr {};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    int fd;

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        DIE("bad server IP %s (use a dotted-quad)", host);
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        DIE("socket: %s", strerror(errno));
    }
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
        DIE("connect %s:%d: %s", host, TCP_PORT, strerror(errno));
    }
    return fd;
}

/* ===== Blackhole side (server only) - UMD in place of raw ioctls ===== */

static int run_server(tt::ChipId chip_id, const char* rdma_name, int gid_index) {
    struct conn c {};

    struct exch local {};

    struct exch remote {};
    struct ibv_mr* bh_mr;
    int dmabuf_fd;
    int sock;
    uint8_t sync;

    // Cluster() enumerates and brings up devices; claim full power domains before doing any RDMA,
    // matching the raw-ioctl demo's raise_power()/TENSTORRENT_IOCTL_SET_POWER_STATE call.
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->get_tt_device(chip_id)->set_power_state(true);

    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
    std::vector<CoreCoord> dram_cores = soc_desc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        DIE("no DRAM cores found on chip %d", static_cast<int>(chip_id));
    }
    CoreCoord core = dram_cores.front();

    // Aim a TLB window at the DRAM tile and export it as a dma-buf. export_dmabuf() folds
    // together what the raw-ioctl demo does by hand (ALLOCATE_TLB, CONFIGURE_TLB,
    // EXPORT_TLB_DMABUF), rounding the underlying window up to the arch's next valid size class
    // internally, so this needs no manual window-size bookkeeping.
    dmabuf_fd = cluster->export_dmabuf(chip_id, core, NOC_ADDR, XFER_SIZE);

    printf(
        "server: BH chip %d core=(x=%zu,y=%zu,addr=0x%llx) %zu MiB exported\n",
        static_cast<int>(chip_id),
        core.x,
        core.y,
        static_cast<unsigned long long>(NOC_ADDR),
        static_cast<size_t>(XFER_SIZE >> 20));

    setup_verbs(&c, rdma_name, gid_index, &local);

    // Register the exported DRAM window as an RDMA MR. This is the only place the dma-buf fd is
    // used; the server never maps or touches the data itself.
    bh_mr = ibv_reg_dmabuf_mr(
        c.pd, 0, XFER_SIZE, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!bh_mr) {
        DIE("ibv_reg_dmabuf_mr failed: %s", strerror(errno));
    }
    local.rkey = bh_mr->rkey;
    local.addr = 0;  // iova
    printf("server: bh_mr rkey=0x%x\n", bh_mr->rkey);

    sock = tcp_listen_accept();
    rw_all(sock, &remote, sizeof(remote), 0);  // recv client's QP info
    rw_all(sock, &local, sizeof(local), 1);    // send ours (incl rkey/addr)
    connect_qp(&c, &local, &remote);
    printf("server: connected to client (qpn=0x%x)\n", remote.qpn);

    // The client drives all DMA; we just wait for it to report completion.
    rw_all(sock, &sync, 1, 0);
    printf("server: client reported DMA round-trip verified\n");
    printf("\nNIC <-> Blackhole DMA round-trip verified over the fabric.\n");

    close(sock);
    ibv_dereg_mr(bh_mr);
    close(dmabuf_fd);  // releases the kmd-side pin; UMD returns the TLB window to its pool
    return 0;
}

/* ===== peer side (client only) - drives all DMA, no UMD involved ===== */

static int run_client(const char* server_ip, const char* rdma_name, int gid_index) {
    struct conn c {};

    struct exch local {};

    struct exch remote {};
    struct ibv_mr* src_mr;
    struct ibv_mr* dst_mr;
    size_t nwords = XFER_SIZE / sizeof(uint64_t);
    int sock;
    uint8_t sync = 1;
    double t0;
    double t1;

    setup_verbs(&c, rdma_name, gid_index, &local);

    // Two registered DMA buffers: src is written into Blackhole, dst is filled back from
    // Blackhole. After a correct round-trip they are identical.
    std::vector<uint64_t> src(nwords);
    std::vector<uint64_t> dst(nwords);
    src_mr = ibv_reg_mr(c.pd, src.data(), XFER_SIZE, IBV_ACCESS_LOCAL_WRITE);
    dst_mr = ibv_reg_mr(c.pd, dst.data(), XFER_SIZE, IBV_ACCESS_LOCAL_WRITE);
    if (!src_mr || !dst_mr) {
        DIE("ibv_reg_mr failed: %s", strerror(errno));
    }

    sock = tcp_connect(server_ip);
    rw_all(sock, &local, sizeof(local), 1);    // send our QP info
    rw_all(sock, &remote, sizeof(remote), 0);  // recv server's (incl rkey/addr)
    connect_qp(&c, &local, &remote);
    printf("client: connected to server (qpn=0x%x, bh rkey=0x%x)\n", remote.qpn, remote.rkey);

    // NIC -> BH: DMA a fresh random pattern from src into Blackhole DRAM.
    fill_random(src.data(), XFER_SIZE);
    t0 = now_sec();
    rdma_xfer(
        &c,
        IBV_WR_RDMA_WRITE,
        src_mr,
        reinterpret_cast<const uint8_t*>(src.data()),
        XFER_SIZE,
        remote.addr,
        remote.rkey);
    t1 = now_sec();
    report("DMA write (NIC->BH)", XFER_SIZE, t1 - t0);

    // BH -> NIC: DMA it back out into dst and verify src and dst now match. Zero dst first so a
    // short/silent read can't leave stale data that matches.
    std::fill(dst.begin(), dst.end(), 0);
    t0 = now_sec();
    rdma_xfer(
        &c,
        IBV_WR_RDMA_READ,
        dst_mr,
        reinterpret_cast<const uint8_t*>(dst.data()),
        XFER_SIZE,
        remote.addr,
        remote.rkey);
    t1 = now_sec();
    report("DMA read  (BH->NIC)", XFER_SIZE, t1 - t0);

    for (size_t j = 0; j < nwords; j++) {
        if (dst[j] != src[j]) {
            DIE("verify FAILED at word %zu: got=0x%016" PRIx64 " want=0x%016" PRIx64, j, dst[j], src[j]);
        }
    }
    printf("client: round-trip verified (src and dst identical)\n");

    rw_all(sock, &sync, 1, 1);  // tell server "all verified"

    close(sock);
    ibv_dereg_mr(src_mr);
    ibv_dereg_mr(dst_mr);
    return 0;
}

int main(int argc, char** argv) {
    tt::ChipId chip_id = 0;
    const char* rdma_name = nullptr;
    int gid_index = -1;
    const char* peer;
    int opt;

    while ((opt = getopt(argc, argv, "c:r:g:h")) != -1) {
        switch (opt) {
            case 'c':
                chip_id = static_cast<tt::ChipId>(atoi(optarg));
                break;
            case 'r':
                rdma_name = optarg;
                break;
            case 'g':
                gid_index = atoi(optarg);
                break;
            default:
                printf(
                    "usage:\n"
                    "  server: %s -c chip_id -r rdma_dev [-g gid]\n"
                    "  client: %s -r rdma_dev [-g gid] <server_ip>\n",
                    argv[0],
                    argv[0]);
                return opt == 'h' ? 0 : 2;
        }
    }

    srand48(getpid());
    peer = (optind < argc) ? argv[optind] : nullptr;

    if (!rdma_name) {
        DIE("missing -r rdma_dev (e.g. -r rocep201s0f0)");
    }

    if (peer) {
        return run_client(peer, rdma_name, gid_index);
    }

    return run_server(chip_id, rdma_name, gid_index);
}
