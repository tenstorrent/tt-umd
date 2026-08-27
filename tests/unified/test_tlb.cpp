// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>
#ifdef UMD_HAS_IBVERBS
#include <infiniband/verbs.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/io_window/io_window.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/kmd_versions.hpp"
#include "umd/device/utils/mmio_timeout_config.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

bool is_kmd_version_good() {
    SemVer kmd_ver = PCIDevice::read_kmd_version();

    return kmd_ver.major > 1 || (kmd_ver.major == 1 && kmd_ver.minor >= 34);
}

// Every TestTlb case drives a TLB window directly (raw SiliconTlbWindow / static TLB window), so the
// op carries no hang-detector veto: a single MMIO transfer that stalls on a contended host would trip
// the tight default per-op budget and throw DeviceTimeoutError. Widen the budget for the duration of
// each test and restore the default afterwards so the override never leaks into other tests.
class TestTlb : public ::testing::Test {
protected:
    void SetUp() override { MmioTimeoutConfig::set_op_timeout(std::chrono::milliseconds(100)); }

    void TearDown() override { MmioTimeoutConfig::set_op_timeout(timeout::MMIO_OP_TIMEOUT); }
};

TEST_F(TestTlb, TestTlbWindowAllocateNew) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const uint64_t tensix_addr = 0;
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    uint32_t val = 0;
    std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    for (CoreCoord core : tensix_cores) {
        cluster->write_to_device(&val, sizeof(uint32_t), chip, core, tensix_addr);
        val++;
    }

    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    uint32_t value_check = 0;

    for (CoreCoord core : tensix_cores) {
        tlb_data config;
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_window =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config);

        uint32_t readback_value = tlb_window->read32(0);

        EXPECT_EQ(readback_value, value_check);

        value_check++;
    }
}

TEST_F(TestTlb, TestClusterExportDmabuf) {
    if (PCIDevice::read_kmd_version() < KMD_TLB_DMABUF_EXPORT) {
        GTEST_SKIP() << "KMD version " << PCIDevice::read_kmd_version().str() << " is below required "
                     << KMD_TLB_DMABUF_EXPORT.str();
    }

    if (!tt::umd::utils::has_any_active_rdma_port()) {
        GTEST_SKIP() << "No active RDMA NIC (RoCE/InfiniBand) found under /sys/class/infiniband.";
    }

    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    ASSERT_FALSE(tensix_cores.empty());
    CoreCoord core = tensix_cores.front();

    int fd = cluster->export_dmabuf(chip, core, 0, two_mb_size);
    EXPECT_GE(fd, 0);
    EXPECT_GE(fcntl(fd, F_GETFD), 0);
    close(fd);

    // An address that is page-aligned but not TLB-window-aligned exercises TlbWindow's
    // offset_from_aligned_addr translation: the window's NOC base is rounded down to a size-aligned
    // boundary, so the export starts `addr % window_size` bytes into the window, not at its base.
    const uint64_t page_size = static_cast<uint64_t>(getpagesize());
    fd = cluster->export_dmabuf(chip, core, page_size, page_size);
    EXPECT_GE(fd, 0);
    EXPECT_GE(fcntl(fd, F_GETFD), 0);
    close(fd);

    // Misaligned requests are rejected up front, before a window is allocated or an ioctl issued.
    EXPECT_THROW(cluster->export_dmabuf(chip, core, 1, page_size), std::runtime_error);
    EXPECT_THROW(cluster->export_dmabuf(chip, core, 0, page_size + 1), std::runtime_error);
}

#ifdef UMD_HAS_IBVERBS
namespace {

// Minimal same-host RDMA loopback RC connection: two QPs on one port, cross-connected using the
// port's own GID, so an RDMA READ issued on one QP is served by the other without ever leaving the
// host. Used by TestClusterExportDmabufLoopback below to confirm that a dma-buf exported via
// Cluster::export_dmabuf() is real, NIC-DMA-readable device memory, not just a valid fd.

constexpr uint8_t RDMA_LOOPBACK_SENTINEL = 0xAA;

bool gid_is_rocev2(const char* rdma_name, uint8_t port, int index) {
    char path[256];
    char buf[64];

    snprintf(path, sizeof(path), "/sys/class/infiniband/%s/ports/%u/gid_attrs/types/%d", rdma_name, port, index);
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

int pick_rocev2_gid(struct ibv_context* ctx, const char* rdma_name, uint8_t port) {
    static const uint8_t v4_mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

    union ibv_gid gid {};

    for (int i = 0; i < 16; i++) {
        if (ibv_query_gid(ctx, port, i, &gid)) {
            break;
        }
        if (memcmp(gid.raw, v4_mapped_prefix, sizeof(v4_mapped_prefix)) != 0) {
            continue;
        }
        if (gid_is_rocev2(rdma_name, port, i)) {
            return i;
        }
    }
    return -1;
}

// Opens the first RDMA device with at least one ACTIVE port, and reports its name + that port.
struct ibv_context* open_first_active_rdma_device(std::string& out_name, uint8_t& out_port) {
    int n = 0;
    struct ibv_device** list = ibv_get_device_list(&n);
    if (!list) {
        return nullptr;
    }

    struct ibv_context* ctx = nullptr;
    for (int i = 0; i < n && !ctx; i++) {
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
                ctx = candidate;
                out_name = ibv_get_device_name(list[i]);
                out_port = port;
                break;
            }
        }
        if (!ctx) {
            ibv_close_device(candidate);
        }
    }
    ibv_free_device_list(list);
    return ctx;
}

struct LoopbackQpPair {
    struct ibv_context* ctx = nullptr;
    struct ibv_pd* pd = nullptr;
    struct ibv_cq* cq = nullptr;
    struct ibv_qp* qp_req = nullptr;  // requester: issues the RDMA READs
    struct ibv_qp* qp_rsp = nullptr;  // responder: serves them out of the dma-buf MR
    uint8_t port = 1;
    int gid_index = -1;
    // Two different device limits, clamped separately:
    //   init = how many RDMA reads this QP may have outstanding as requester (max_qp_init_rd_atom)
    //   dest = how many it may have inbound as responder            (max_qp_rd_atom)
    int rd_atomic_init = 1;
    int rd_atomic_dest = 1;

    union ibv_gid gid {};

    ~LoopbackQpPair() {
        if (qp_req) {
            ibv_destroy_qp(qp_req);
        }
        if (qp_rsp) {
            ibv_destroy_qp(qp_rsp);
        }
        if (cq) {
            ibv_destroy_cq(cq);
        }
        if (pd) {
            ibv_dealloc_pd(pd);
        }
        if (ctx) {
            ibv_close_device(ctx);
        }
    }
};

struct ibv_qp* make_qp(LoopbackQpPair& c, int sq_depth) {
    struct ibv_qp_init_attr ia {};

    ia.send_cq = c.cq;
    ia.recv_cq = c.cq;
    ia.qp_type = IBV_QPT_RC;
    ia.cap.max_send_wr = static_cast<uint32_t>(sq_depth);
    ia.cap.max_recv_wr = 4;
    ia.cap.max_send_sge = 1;
    ia.cap.max_recv_sge = 1;

    struct ibv_qp* qp = ibv_create_qp(c.pd, &ia);
    if (!qp) {
        throw std::runtime_error(std::string("ibv_create_qp failed: ") + strerror(errno));
    }

    struct ibv_qp_attr init {};

    init.qp_state = IBV_QPS_INIT;
    init.pkey_index = 0;
    init.port_num = c.port;
    // Both QPs get the full set: the responder needs REMOTE_READ to serve reads out of the
    // dma-buf MR, and granting it symmetrically keeps the two setup paths identical.
    init.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(qp, &init, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        throw std::runtime_error(std::string("modify INIT failed: ") + strerror(errno));
    }
    return qp;
}

// Moves `qp` to RTR pointing at `dest_qpn`, then to RTS. The destination GID is our own GID - that
// is what makes this a loopback: the NIC recognises the target as local and turns the traffic
// around internally rather than putting it on the wire.
void connect_qp(
    LoopbackQpPair& c, struct ibv_qp* qp, uint32_t dest_qpn, uint32_t dest_psn, uint32_t local_psn, enum ibv_mtu mtu) {
    struct ibv_qp_attr rtr {};

    rtr.qp_state = IBV_QPS_RTR;
    rtr.path_mtu = mtu;
    rtr.dest_qp_num = dest_qpn;
    rtr.rq_psn = dest_psn;
    rtr.max_dest_rd_atomic = static_cast<uint8_t>(c.rd_atomic_dest);
    rtr.min_rnr_timer = 12;
    rtr.ah_attr.is_global = 1;
    rtr.ah_attr.port_num = c.port;
    rtr.ah_attr.grh.sgid_index = c.gid_index;
    rtr.ah_attr.grh.hop_limit = 64;
    memcpy(rtr.ah_attr.grh.dgid.raw, c.gid.raw, 16);

    if (ibv_modify_qp(
            qp,
            &rtr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER)) {
        throw std::runtime_error(std::string("modify RTR failed: ") + strerror(errno));
    }

    struct ibv_qp_attr rts {};

    rts.qp_state = IBV_QPS_RTS;
    rts.timeout = 14;
    rts.retry_cnt = 7;
    rts.rnr_retry = 7;
    rts.sq_psn = local_psn;
    rts.max_rd_atomic = static_cast<uint8_t>(c.rd_atomic_init);
    if (ibv_modify_qp(
            qp,
            &rts,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC)) {
        throw std::runtime_error(std::string("modify RTS failed: ") + strerror(errno));
    }
}

void setup_loopback_qp_pair(LoopbackQpPair& c, int outstanding) {
    std::string dev_name;
    c.ctx = open_first_active_rdma_device(dev_name, c.port);
    if (!c.ctx) {
        throw std::runtime_error("no active RDMA device found");
    }

    c.pd = ibv_alloc_pd(c.ctx);
    if (!c.pd) {
        throw std::runtime_error("ibv_alloc_pd failed");
    }

    // Sized for both QPs' completions even though only the requester generates any (RDMA READ is
    // one-sided: the responder produces no CQE).
    c.cq = ibv_create_cq(c.ctx, outstanding * 2 + 16, nullptr, nullptr, 0);
    if (!c.cq) {
        throw std::runtime_error("ibv_create_cq failed");
    }

    c.gid_index = pick_rocev2_gid(c.ctx, dev_name.c_str(), c.port);
    if (c.gid_index < 0) {
        throw std::runtime_error("no RoCEv2 (IPv4-mapped) GID found on " + dev_name);
    }
    if (ibv_query_gid(c.ctx, c.port, c.gid_index, &c.gid)) {
        throw std::runtime_error("ibv_query_gid failed");
    }

    // Outstanding RDMA READs are capped by the QP's max_rd_atomic, which in turn cannot exceed
    // what the HCA reports.
    struct ibv_device_attr dev_attr {};

    int hw_init = 16;
    int hw_dest = 16;
    if (ibv_query_device(c.ctx, &dev_attr) == 0) {
        if (dev_attr.max_qp_init_rd_atom > 0) {
            hw_init = dev_attr.max_qp_init_rd_atom;
        }
        if (dev_attr.max_qp_rd_atom > 0) {
            hw_dest = dev_attr.max_qp_rd_atom;
        }
    }
    // Both attributes are capped at 255 by the uint8_t fields in ibv_qp_attr.
    c.rd_atomic_init = std::min({outstanding, hw_init, 255});
    c.rd_atomic_dest = std::min({outstanding, hw_dest, 255});

    enum ibv_mtu mtu = IBV_MTU_4096;

    struct ibv_port_attr pa {};

    if (!ibv_query_port(c.ctx, c.port, &pa)) {
        mtu = std::min(pa.active_mtu, mtu);
    }

    const int sq_depth = outstanding + 8;
    c.qp_req = make_qp(c, sq_depth);
    c.qp_rsp = make_qp(c, sq_depth);

    const uint32_t psn_req = 0x111111;
    const uint32_t psn_rsp = 0x222222;
    connect_qp(c, c.qp_req, c.qp_rsp->qp_num, psn_rsp, psn_req, mtu);
    connect_qp(c, c.qp_rsp, c.qp_req->qp_num, psn_req, psn_rsp, mtu);
}

// Chunked, pipelined RDMA READ over [0, total) into host memory at `host_base`. The dma-buf MR is
// registered with iova 0, so remote_addr is just the byte offset.
void rdma_read(
    LoopbackQpPair& c,
    struct ibv_mr* local_mr,
    uintptr_t host_base,
    size_t total,
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
            wr.wr.rdma.remote_addr = off;
            wr.wr.rdma.rkey = rkey;

            struct ibv_send_wr* bad = nullptr;
            if (int rc = ibv_post_send(c.qp_req, &wr, &bad)) {
                throw std::runtime_error(std::string("ibv_post_send failed: ") + strerror(rc));
            }
            off += len;
            inflight++;
        }

        struct ibv_wc wc {};

        int n = ibv_poll_cq(c.cq, 1, &wc);
        if (n < 0) {
            throw std::runtime_error("ibv_poll_cq failed");
        }
        if (n > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                throw std::runtime_error(std::string("completion error: ") + ibv_wc_status_str(wc.status));
            }
            inflight--;
        }
    }
}

}  // namespace
#endif  // UMD_HAS_IBVERBS

// DISABLED_ so it doesn't run automatically as part of the suite / CI; run explicitly with
// --gtest_also_run_disabled_tests --gtest_filter=*TestClusterExportDmabufLoopback when RDMA
// hardware is available.
TEST_F(TestTlb, DISABLED_TestClusterExportDmabufLoopback) {
#ifndef UMD_HAS_IBVERBS
    GTEST_SKIP() << "libibverbs-dev not available at build time.";
#else
    if (PCIDevice::read_kmd_version() < KMD_TLB_DMABUF_EXPORT) {
        GTEST_SKIP() << "KMD version " << PCIDevice::read_kmd_version().str() << " is below required "
                     << KMD_TLB_DMABUF_EXPORT.str();
    }

    if (!tt::umd::utils::has_any_active_rdma_port()) {
        GTEST_SKIP() << "No active RDMA NIC (RoCE/InfiniBand) found under /sys/class/infiniband.";
    }

    const ChipId chip = 0;
    const uint64_t size = 1 << 21;  // 2 MiB: a valid tt_tlb_alloc size class on both Wormhole and Blackhole.
    const size_t nwords = size / sizeof(uint64_t);
    const size_t chunk = 512 * 1024;
    const int outstanding = 8;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    std::vector<CoreCoord> dram_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    ASSERT_FALSE(dram_cores.empty());
    CoreCoord core = dram_cores.front();

    // Golden-ratio multiplier: gives a distinct, non-trivial value per word index, so a partial or
    // misaddressed transfer cannot accidentally verify.
    constexpr uint64_t pattern_mult = 0x9E3779B97F4A7C15ULL;
    std::vector<uint64_t> pattern(nwords);
    for (size_t i = 0; i < nwords; i++) {
        pattern[i] = static_cast<uint64_t>(i) * pattern_mult;
    }

    // write_to_device() lands on a different TLB window than the dedicated one export_dmabuf()
    // allocates below, so a passing verify below is independent evidence that the exported window
    // really reaches device DRAM, rather than a self-consistent "write and read back through the
    // same window" check.
    cluster->write_to_device(pattern.data(), size, chip, core, 0);

    int dmabuf_fd = cluster->export_dmabuf(chip, core, 0, size);
    ASSERT_GE(dmabuf_fd, 0);

    LoopbackQpPair qp_pair;
    setup_loopback_qp_pair(qp_pair, outstanding);

    // iova 0, so remote_addr on the READ is a plain byte offset.
    struct ibv_mr* dev_mr = ibv_reg_dmabuf_mr(
        qp_pair.pd, 0, size, 0, dmabuf_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    ASSERT_NE(dev_mr, nullptr) << "ibv_reg_dmabuf_mr failed: " << strerror(errno);

    std::vector<uint64_t> host_buf(nwords);
    std::fill(
        reinterpret_cast<uint8_t*>(host_buf.data()),
        reinterpret_cast<uint8_t*>(host_buf.data()) + size,
        RDMA_LOOPBACK_SENTINEL);
    struct ibv_mr* host_mr = ibv_reg_mr(qp_pair.pd, host_buf.data(), size, IBV_ACCESS_LOCAL_WRITE);
    ASSERT_NE(host_mr, nullptr) << "ibv_reg_mr failed: " << strerror(errno);

    rdma_read(qp_pair, host_mr, reinterpret_cast<uintptr_t>(host_buf.data()), size, dev_mr->rkey, chunk, outstanding);

    EXPECT_EQ(host_buf, pattern) << "RDMA READ over the exported dma-buf did not match the UMD-seeded pattern";

    ibv_dereg_mr(host_mr);
    ibv_dereg_mr(dev_mr);
    close(dmabuf_fd);
#endif  // UMD_HAS_IBVERBS
}

TEST_F(TestTlb, TestTlbWindowReuse) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const uint64_t tensix_addr = 0;
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    uint32_t val = 0;
    std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    for (CoreCoord core : tensix_cores) {
        cluster->write_to_device(&val, sizeof(uint32_t), chip, core, tensix_addr);
        val++;
    }

    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    uint32_t value_check = 0;

    // Here it's not important how we have configured the TLB. For every read we will
    // do the reconfigure of the TLB window.
    tlb_data config{};
    std::unique_ptr<TlbWindow> tlb_window =
        std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config);

    for (CoreCoord core : tensix_cores) {
        tlb_data config;
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        tlb_window->configure(config);

        uint32_t readback_value = tlb_window->read32(0);

        EXPECT_EQ(readback_value, value_check);

        value_check++;
    }
}

// TODO: debug this test failing on T3K.
TEST_F(TestTlb, DISABLED_TestTlbWindowReadRegister) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    // Point of the test is to read NOC0 node id register.
    // TLB needs to be aligned to 2MB so these base and offset values are
    // how TLB should be programmed in order to get to addr 0xFFB2002C.
    const uint64_t tlb_base = 0xFFA00000;
    const uint64_t noc_node_id_tlb_offset = 0x12002C;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    PCIDevice* pci_device = cluster->get_tt_device(0)->get_pci_device();

    const std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    for (CoreCoord core : tensix_cores) {
        tlb_data config;
        config.local_offset = tlb_base & ~(two_mb_size - 1);
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Strict;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_window =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::UC), config);

        tlb_window->configure(config);

        uint32_t noc_node_id_val = tlb_window->read32(noc_node_id_tlb_offset & (two_mb_size - 1));

        uint32_t x = noc_node_id_val & 0x3F;
        uint32_t y = (noc_node_id_val >> 6) & 0x3F;

        EXPECT_EQ(core.x, x);
        EXPECT_EQ(core.y, y);
    }
}

TEST_F(TestTlb, TestTlbWindowReadWrite) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    for (CoreCoord core : tensix_cores) {
        tlb_data config_write;
        config_write.local_offset = 0;
        config_write.x_end = core.x;
        config_write.y_end = core.y;
        config_write.x_start = 0;
        config_write.y_start = 0;
        config_write.noc_sel = 0;
        config_write.mcast = 0;
        config_write.ordering = tlb_data::Relaxed;
        config_write.linked = 0;
        config_write.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_window_write =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config_write);

        tlb_window_write->write32(0, 4);
        tlb_window_write->write32(4, 0);

        tlb_data config_read = config_write;
        std::unique_ptr<TlbWindow> tlb_window_read =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config_read);

        uint32_t expect4 = tlb_window_read->read32(0);
        uint32_t expect0 = tlb_window_read->read32(4);

        EXPECT_EQ(expect4, 4);
        EXPECT_EQ(expect0, 0);
    }
}

TEST_F(TestTlb, TestTlbWindowReadWrite16) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    for (CoreCoord core : tensix_cores) {
        tlb_data config;
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_write =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config);
        std::unique_ptr<TlbWindow> tlb_read =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config);

        // Test basic write16/read16.
        tlb_write->write16(0, 0xABCD);
        uint16_t readback16 = tlb_read->read16(0);
        EXPECT_EQ(readback16, 0xABCD);

        tlb_write->write16(2, 0x1234);
        readback16 = tlb_read->read16(2);
        EXPECT_EQ(readback16, 0x1234);

        // Two write16 calls should be equivalent to one write32.
        // Write via two write16 at offset 0, then read back as write32.
        const uint16_t low16 = 0xBEEF;
        const uint16_t high16 = 0xDEAD;
        const uint32_t combined32 = (static_cast<uint32_t>(high16) << 16) | low16;

        tlb_write->write16(0, low16);
        tlb_write->write16(2, high16);
        uint32_t readback32 = tlb_read->read32(0);
        EXPECT_EQ(readback32, combined32);

        // Conversely, write32 and read back as two read16.
        const uint32_t test_val32 = 0xCAFEBABE;
        tlb_write->write32(4, test_val32);
        uint16_t low_half = tlb_read->read16(4);
        uint16_t high_half = tlb_read->read16(6);
        EXPECT_EQ(low_half, static_cast<uint16_t>(test_val32 & 0xFFFF));
        EXPECT_EQ(high_half, static_cast<uint16_t>((test_val32 >> 16) & 0xFFFF));

        // Write a full 32-bit pattern via write16, then verify equivalence with a direct write32 on a different offset.
        tlb_write->write16(8, 0x1111);
        tlb_write->write16(10, 0x2222);
        tlb_write->write32(12, 0x22221111);
        uint32_t from_16 = tlb_read->read32(8);
        uint32_t from_32 = tlb_read->read32(12);
        EXPECT_EQ(from_16, from_32);
    }
}

TEST_F(TestTlb, TestTlbWrite16DoesNotCorruptAdjacentData) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    for (CoreCoord core : tensix_cores) {
        tlb_data config;
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_write =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config);
        std::unique_ptr<TlbWindow> tlb_read =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb_size, TlbMapping::WC), config);

        // Write a known 32-bit value, then overwrite only the low half with write16.
        tlb_write->write32(0, 0xAAAABBBB);
        tlb_write->write16(0, 0xCCCC);
        EXPECT_EQ(tlb_read->read16(0), 0xCCCC) << "Low half should be updated by write16";
        EXPECT_EQ(tlb_read->read16(2), 0xAAAA) << "High half should be untouched by write16 to low half";
        EXPECT_EQ(tlb_read->read32(0), 0xAAAACCCC);

        // Now overwrite only the high half with write16.
        tlb_write->write16(2, 0xDDDD);
        EXPECT_EQ(tlb_read->read16(2), 0xDDDD) << "High half should be updated by write16";
        EXPECT_EQ(tlb_read->read16(0), 0xCCCC) << "Low half should be untouched by write16 to high half";
        EXPECT_EQ(tlb_read->read32(0), 0xDDDDCCCC);

        // Verify across two adjacent 32-bit words: writing to one does not affect the other.
        tlb_write->write32(4, 0x11112222);
        tlb_write->write32(8, 0x33334444);
        tlb_write->write16(4, 0x5555);
        EXPECT_EQ(tlb_read->read32(4), 0x11115555) << "Only low half of first word should change";
        EXPECT_EQ(tlb_read->read32(8), 0x33334444) << "Second word should be completely untouched";
    }
}

TEST_F(TestTlb, TestTlbOffsetReadWrite) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t two_mb = 1 << 21;
    const uint64_t one_mb = 1 << 20;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    std::vector<uint8_t> write_pattern(0x100, 0);
    for (size_t i = 0; i < write_pattern.size(); ++i) {
        write_pattern[i] = (i % 256);
    }

    for (CoreCoord core : tensix_cores) {
        cluster->write_to_device(write_pattern.data(), write_pattern.size(), chip, core, one_mb);

        tlb_data config;
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> read_aligned =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb, TlbMapping::WC), config);

        config.local_offset = one_mb;
        std::unique_ptr<TlbWindow> read_unaligned =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb, TlbMapping::WC), config);

        std::vector<uint8_t> readback_aligned(0x100, 0);
        read_aligned->read_block(one_mb, readback_aligned.data(), readback_aligned.size());

        EXPECT_EQ(readback_aligned, write_pattern)
            << "Readback data from aligned TLB window should match the written pattern";

        std::vector<uint8_t> readback_unaligned(0x100, 0);
        read_unaligned->read_block(0, readback_unaligned.data(), readback_unaligned.size());

        EXPECT_EQ(readback_aligned, readback_unaligned)
            << "Readback data from aligned and unaligned TLB windows should be the same";

        config.local_offset = (one_mb >> 1);
        read_unaligned->configure(config);
        std::vector<uint8_t> readback_unaligned_1(0x100, 0);
        read_unaligned->read_block(one_mb >> 1, readback_unaligned_1.data(), readback_unaligned_1.size());

        EXPECT_EQ(readback_unaligned_1, write_pattern)
            << "Readback data from unaligned TLB window with offset should match the written pattern";
    }
}

TEST_F(TestTlb, TestTlbAccessOutofBounds) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t two_mb = 1 << 21;
    const uint64_t one_mb = 1 << 20;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();

    for (CoreCoord core : tensix_cores) {
        tlb_data config;
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> read_aligned =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb, TlbMapping::WC), config);

        config.local_offset = one_mb;
        std::unique_ptr<TlbWindow> read_unaligned =
            std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(two_mb, TlbMapping::WC), config);

        std::vector<uint8_t> readback_aligned(0x100, 0);
        read_aligned->read_block(one_mb, readback_aligned.data(), readback_aligned.size());

        std::vector<uint8_t> readback_unaligned(0x100, 0);
        read_unaligned->read_block(0, readback_unaligned.data(), readback_unaligned.size());

        EXPECT_EQ(readback_aligned, readback_unaligned)
            << "Readback data from aligned and unaligned TLB windows should be the same";

        std::vector<uint8_t> readback_out_of_bounds(two_mb + 1, 0);
        EXPECT_ANY_THROW(read_aligned->read_block(0, readback_out_of_bounds.data(), readback_out_of_bounds.size()))
            << "Reading out of bounds from TLB window should throw an exception";

        std::vector<uint8_t> readback_out_of_bounds_unaligned(one_mb + 1, 0);
        EXPECT_ANY_THROW(read_unaligned->read_block(
            0, readback_out_of_bounds_unaligned.data(), readback_out_of_bounds_unaligned.size()))
            << "Reading out of bounds from TLB window should throw an exception";
    }
}

// Exercises a TLB window purely through the Base API IoWindow interface: every call below goes
// through an IoWindow& rather than the concrete type, so this fails if TlbWindow ever stops
// satisfying the spec surface.
TEST_F(TestTlb, IoWindowInterface) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    // 2MB is a valid TLB size class on both Wormhole and Blackhole.
    const size_t window_size = 1 << 21;
    // Unaligned within the window, so get_target_config() has to reconstruct the sub-window offset.
    const uint64_t l1_addr = 0x100;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();
    const CoreCoord tensix_core =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    SiliconTlbWindow tlb_window(pci_device->allocate_tlb(window_size, TlbMapping::WC));
    IoWindow& window = tlb_window;

    TargetIoWindowConfig target;
    target.core_start = tt_xy_pair(tensix_core.x, tensix_core.y);
    target.addr = l1_addr;
    target.noc = NocId::NOC0;
    window.configure(target);

    // Host-side properties come from the allocation, not from the target.
    EXPECT_EQ(window.get_memory_caching_type(), HostMemoryCaching::WC);
    EXPECT_EQ(window.get_size(), window_size - l1_addr);

    const TargetIoWindowConfig readback_target = window.get_target_config();
    EXPECT_EQ(readback_target.core_start, target.core_start);
    EXPECT_FALSE(readback_target.core_end.has_value()) << "Unicast target should not report a multicast grid";
    EXPECT_EQ(readback_target.addr, l1_addr);
    EXPECT_EQ(readback_target.noc, NocId::NOC0);

    // The single-argument configure() is documented to apply Strict.
    EXPECT_EQ(window.get_io_ordering(), IoOrdering::Strict);

    window.write32(0, 0xabcd1234);
    EXPECT_EQ(window.read32(0), 0xabcd1234u);

    window.write16(4, 0x5678);
    EXPECT_EQ(window.read16(4), 0x5678u);

    std::vector<uint8_t> block_pattern(0x100);
    for (size_t i = 0; i < block_pattern.size(); i++) {
        block_pattern[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> block_readback(block_pattern.size(), 0);
    window.write_block(0x200, block_pattern.data(), block_pattern.size());
    window.read_block(0x200, block_readback.data(), block_readback.size());
    EXPECT_EQ(block_readback, block_pattern);

    std::vector<uint32_t> aligned_pattern(0x40);
    for (size_t i = 0; i < aligned_pattern.size(); i++) {
        aligned_pattern[i] = static_cast<uint32_t>(i) | 0xa5000000;
    }
    std::vector<uint32_t> aligned_readback(aligned_pattern.size(), 0);
    const size_t aligned_bytes = aligned_pattern.size() * sizeof(uint32_t);
    window.write_aligned(0x400, aligned_pattern.data(), aligned_bytes);
    window.read_aligned(0x400, aligned_readback.data(), aligned_bytes);
    EXPECT_EQ(aligned_readback, aligned_pattern);

    // Posted is the mode tt-metal needs for the Blackhole DRAM window; make sure it survives configure().
    window.configure(target, IoOrdering::Posted);
    EXPECT_EQ(window.get_io_ordering(), IoOrdering::Posted);
    EXPECT_EQ(window.get_target_config().addr, l1_addr) << "Ordering change should not disturb the target";

    // WindowFlags have no TLB equivalent and must be rejected rather than silently dropped.
    target.flags = WindowFlags::Atomic;
    EXPECT_ANY_THROW(window.configure(target));
}

TEST_F(TestTlb, TLBStaticTensix) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const size_t tlb_size = cluster->get_tt_device(0)->get_arch() == tt::ARCH::WORMHOLE_B0 ? (1 << 20) : (1 << 21);

    const CoreCoord tensix_core_0 = cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX)[0];
    std::vector<uint32_t> zero_out(1024, 0);
    std::vector<uint32_t> readback_zeros(1024, 0xFFFFFFFF);
    cluster->write_to_device(zero_out.data(), zero_out.size() * sizeof(uint32_t), 0, tensix_core_0, 0);
    cluster->read_from_device(readback_zeros.data(), 0, tensix_core_0, 0, readback_zeros.size() * sizeof(uint32_t));

    EXPECT_EQ(readback_zeros, zero_out);

    for (const CoreCoord tensix_core :
         cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
        cluster->configure_tlb(0, tensix_core, tlb_size, 0, tlb_data::Strict);
    }

    TlbWindow* window = cluster->get_static_tlb_window(0, tensix_core_0);

    const int num_writes = 1024;
    for (int i = 0; i < num_writes; i++) {
        window->write32(4 * i, i);
    }

    std::vector<uint32_t> readback(num_writes, 0);
    cluster->read_from_device(readback.data(), 0, tensix_core_0, 0, readback.size() * sizeof(uint32_t));

    for (int i = 0; i < num_writes; i++) {
        EXPECT_EQ(readback[i], i);
    }
}

TEST_F(TestTlb, TestRegisterReconfigureL1RoundTrip) {
    if (!is_kmd_version_good()) {
        GTEST_SKIP() << "Skipping test because of old KMD version. Required version of KMD is 1.34 or higher.";
    }
    const ChipId chip = 0;
    const uint64_t l1_start = 0x100;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();
    const auto& tensix_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);

    const size_t num_words = ((1 << 20) + (1 << 17)) / sizeof(uint32_t);
    const size_t test_size = num_words * sizeof(uint32_t);
    const size_t tlb_size = 1 << 21;

    std::vector<uint32_t> pattern(num_words);
    std::generate(pattern.begin(), pattern.end(), [i = uint32_t{0}]() mutable { return i++ * 0xDEAD0001; });

    const auto cores_end = tensix_cores.begin() + std::min(size_t{4}, tensix_cores.size());
    for (auto it = tensix_cores.begin(); it != cores_end; ++it) {
        tt_xy_pair xy{it->x, it->y};

        auto tlb_window = std::make_unique<SiliconTlbWindow>(pci_device->allocate_tlb(tlb_size, TlbMapping::UC));

        tlb_window->write_register_reconfigure(pattern.data(), xy, l1_start, test_size, NocId::NOC0);

        std::vector<uint32_t> readback(num_words, 0);
        tlb_window->read_register_reconfigure(readback.data(), xy, l1_start, test_size, NocId::NOC0);

        EXPECT_EQ(readback, pattern) << "Mismatch on core " << it->str();
    }
}
