// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "tests/rdma/rdma_loopback.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/kmd_versions.hpp"
#include "umd/device/utils/mmio_timeout_config.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

// End-to-end check that Cluster::export_dmabuf() hands out real, third-party-DMA-capable device
// memory. The sequence is:
//
//   1. UMD writes a known pattern into device DRAM through a normal TLB window (write_to_device).
//   2. export_dmabuf() exports that same DRAM range as a dma-buf fd, which allocates its own,
//      separate TLB window.
//   3. The NIC registers that fd as a memory region and RDMA-READs the whole range into host memory
//      over a same-host loopback RC connection.
//   4. The host buffer is compared against the pattern.
//
// What a pass proves: the exported fd is not merely a valid file descriptor - it maps device DRAM in
// a way a third-party device (the NIC) can DMA out of, at the right physical location and with the
// right length, since the data read back by the NIC matches what UMD wrote through an entirely
// different TLB window. A self-consistent "write and read back through the same window" check could
// not distinguish that from a misprogrammed or bogus export.
//
// Scope: the dma-buf export path is currently intended for Blackhole Galaxy systems and is only
// exercised there. Nothing in this test is arch-specific and the size below is valid on Wormhole too,
// so it is not gated to one arch, but a Wormhole run is unverified.
//
// This target is opt-in and not part of any automatically run test suite: it needs libibverbs at
// build time (-DTT_UMD_BUILD_RDMA=ON) and an ACTIVE RoCEv2 NIC plus a KMD with dma-buf export support
// at run time. Run it explicitly on a machine that has them.
// The pattern write below is a single 2 MiB MMIO transfer through a TLB window, which carries no
// hang-detector veto: on a contended host it can outlast the tight default per-op budget and throw
// DeviceTimeoutError. Widen the budget for the test and restore the default afterwards.
class TestDmabufRdmaLoopback : public ::testing::Test {
protected:
    void SetUp() override { MmioTimeoutConfig::set_op_timeout(std::chrono::milliseconds(100)); }

    void TearDown() override { MmioTimeoutConfig::set_op_timeout(timeout::MMIO_OP_TIMEOUT); }
};

TEST_F(TestDmabufRdmaLoopback, ExportedDmabufIsRdmaReadable) {
    if (PCIDevice::read_kmd_version() < KMD_TLB_DMABUF_EXPORT) {
        GTEST_SKIP() << "KMD version " << PCIDevice::read_kmd_version().str() << " is below required "
                     << KMD_TLB_DMABUF_EXPORT.str();
    }

    if (!tt::umd::utils::has_any_active_rdma_port()) {
        GTEST_SKIP() << "No active RDMA NIC (RoCE/InfiniBand) found under /sys/class/infiniband.";
    }

    const ChipId chip = 0;
    // 2 MiB: a valid tt_tlb_alloc size class on both Wormhole and Blackhole, so the test needs no
    // arch-specific adjustment (see the size class tables in device/arch/architecture_tlbs.cpp).
    const uint64_t size = 1 << 21;
    const size_t nwords = size / sizeof(uint64_t);
    const size_t chunk_size = 512 * 1024;
    const int max_outstanding_reads = 8;
    // Pre-filling the host buffer with a value the pattern never produces means a transfer that
    // silently does nothing fails the comparison instead of matching zero-initialised memory.
    constexpr uint8_t sentinel = 0xAA;

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

    cluster->write_to_device(pattern.data(), size, chip, core, 0);

    int dmabuf_fd = cluster->export_dmabuf(chip, core, 0, size);
    ASSERT_GE(dmabuf_fd, 0);

    std::vector<uint8_t> host_buf(size, sentinel);

    {
        test::RdmaLoopback loopback(max_outstanding_reads);
        loopback.register_local_sink(host_buf.data(), size);
        loopback.register_dmabuf_source(dmabuf_fd, size);
        loopback.read(size, chunk_size);
    }

    close(dmabuf_fd);

    EXPECT_EQ(memcmp(host_buf.data(), pattern.data(), size), 0)
        << "RDMA READ over the exported dma-buf did not match the UMD-seeded pattern";
}
