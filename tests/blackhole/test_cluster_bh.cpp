// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/l1_address_map.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/semver.hpp"

using namespace tt::umd;

constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

static void set_barrier_params(Cluster& cluster) {
    // Populate address map and NOC parameters that the driver needs for memory barriers and remote transactions.
    cluster.set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, DRAM_BARRIER_BASE});
}

// Raw TLB-window allocation (TENSTORRENT_IOCTL_ALLOCATE_TLB) needs KMD 1.34 or newer.
static bool kmd_supports_tlb_alloc() {
    SemVer kmd_ver = PCIDevice::read_kmd_version();
    return kmd_ver.major > 1 || (kmd_ver.major == 1 && kmd_ver.minor >= 34);
}

TEST(ClusterBH, CreateDestroy) {
    DeviceParams default_params;
    for (int i = 0; i < 50; i++) {
        auto cluster_ptr = test_utils::make_default_test_cluster();
        Cluster& cluster = *cluster_ptr;
        set_barrier_params(cluster);
        test_utils::safe_test_cluster_start(&cluster);
        cluster.close_device();
    }
}

TEST(ClusterBH, UnalignedStaticTLB_RW) {
    auto cluster_ptr = test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);

    // Do this only for a single chip to speed up the test.
    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);
    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        // Statically mapping a 2MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
        cluster.configure_tlb(
            chip_id, core, tt::umd::blackhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
    }

    test_utils::safe_test_cluster_start(&cluster);

    std::vector<uint32_t> unaligned_sizes = {3, 14, 21, 255, 362, 430, 1022, 1023, 1025};
    for (const auto& size : unaligned_sizes) {
        std::vector<uint8_t> write_vec(size, 0);
        std::iota(write_vec.begin(), write_vec.end(), static_cast<uint8_t>(size));
        std::vector<uint8_t> readback_vec(size, 0);
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(write_vec.data(), size, chip_id, core, address);
                cluster.wait_for_non_mmio_flush();
                cluster.read_from_device(readback_vec.data(), chip_id, core, address, size);
                ASSERT_EQ(readback_vec, write_vec);
                readback_vec = std::vector<uint8_t>(size, 0);
                cluster.write_to_sysmem(write_vec.data(), size, 0, 0, 0);
                cluster.read_from_sysmem(readback_vec.data(), 0, 0, size, 0);
                ASSERT_EQ(readback_vec, write_vec);
                readback_vec = std::vector<uint8_t>(size, 0);
                cluster.wait_for_non_mmio_flush();
            }
            address += 0x20;
        }
    }
    cluster.close_device();
}

TEST(ClusterBH, StaticTLB_RW) {
    auto cluster_ptr = test_utils::make_default_test_cluster();
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);

    // Do this only for a single chip to speed up the test.
    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);
    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        // Statically mapping a 2MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
        cluster.configure_tlb(
            chip_id, core, tt::umd::blackhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space.
    std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
    // Stress-test TLB stability by exercising one chip 100 times at different statically mapped addresses.
    for (int loop = 0; loop < 100; loop++) {
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
            // Barrier to ensure that all writes over ethernet were commited.
            cluster.wait_for_non_mmio_flush();
            test_utils::read_data_from_device(cluster, readback_vec, chip_id, core, address, 40);
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << core.str() << " does not match what was written";
            cluster.wait_for_non_mmio_flush();
            cluster.write_to_device(
                zeros.data(),
                zeros.size() * sizeof(std::uint32_t),
                chip_id,
                core,
                address);  // Clear any written data
            cluster.wait_for_non_mmio_flush();
            readback_vec = {};
        }
        address += 0x20;  // Increment by uint32_t size for each write
    }
    cluster.close_device();
}

TEST(ClusterBH, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for
    // each transaction
    auto cluster_ptr = test_utils::make_default_test_cluster();
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);

    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};

    std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
    // Stress-test TLB stability by exercising one chip 100 times at different statically mapped addresses.
    for (int loop = 0; loop < 100; loop++) {
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
            // Barrier to ensure that all writes over ethernet were commited.
            cluster.wait_for_non_mmio_flush();
            test_utils::read_data_from_device(cluster, readback_vec, chip_id, core, address, 40);
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << core.str() << " does not match what was written";
            cluster.wait_for_non_mmio_flush();
            cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
            cluster.wait_for_non_mmio_flush();
            readback_vec = {};
        }
        address += 0x20;  // Increment by uint32_t size for each write
    }
    printf("Target Tensix cores completed\n");

    // Target DRAM channel 0.
    std::vector<uint32_t> dram_vector_to_write = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    address = 0x400;
    int NUM_CHANNELS = sdesc.get_num_dram_channels();
    for (int loop = 0; loop < 100; loop++) {
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            std::vector<CoreCoord> chan = sdesc.get_dram_cores().at(ch);
            CoreCoord subchan = chan.at(0);
            cluster.write_to_device(
                vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, subchan, address);
            cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
            test_utils::read_data_from_device(cluster, readback_vec, chip_id, subchan, address, 40);
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << subchan.x << "-" << subchan.y << "does not match what was written";
            cluster.wait_for_non_mmio_flush();
            cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, subchan, address);
            cluster.wait_for_non_mmio_flush();
            readback_vec = {};
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    printf("Target DRAM completed\n");

    cluster.close_device();
}

// TODO(#2485): Re-enable. Writes and reads are not synchronized so they can land on the device out of order; broke
// after PR #2455.
TEST(ClusterBH, DISABLED_MultiThreadedDevice) {
    // Have 2 threads read and write from a single device concurrently
    // All transactions go through a single Dynamic TLB. We want to make sure this is thread/process safe.
    auto cluster_ptr = test_utils::make_default_test_cluster();
    Cluster& cluster = *cluster_ptr;

    set_barrier_params(cluster);

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), 0, core, address);
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 40);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x30000000;
        for (const std::vector<CoreCoord>& core_ls : cluster.get_soc_descriptor(0).get_dram_cores()) {
            for (int loop = 0; loop < 100; loop++) {
                for (const CoreCoord& core : core_ls) {
                    cluster.write_to_device(
                        vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), 0, core, address);
                    test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 40);
                    ASSERT_EQ(vector_to_write, readback_vec)
                        << "Vector read back from core " << core.str() << " does not match what was written";
                    readback_vec = {};
                }
                address += 0x20;
            }
        }
    });

    th1.join();
    th2.join();
    cluster.close_device();
}

TEST(ClusterBH, MultiThreadedMemBar) {
    // Have 2 threads read and write from a single device concurrently
    // All (fairly large) transactions go through a static TLB.
    // We want to make sure the memory barrier is thread/process safe.

    // Memory barrier flags get sent to address 0 for all channels in this test.
    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    auto cluster_ptr = test_utils::make_default_test_cluster();
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);
    for (auto chip_id : cluster.get_target_device_ids()) {
        // Iterate over devices and only setup static TLBs for functional worker cores.
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 2MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE.
            cluster.configure_tlb(chip_id, core, tt::umd::blackhole::STATIC_TLB_SIZE, base_addr);
        }
    }

    std::vector<uint32_t> readback_membar_vec = {};
    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, l1_mem::address_map::L1_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers were correctly initialized on all workers
        readback_membar_vec = {};
    }

    for (int chan = 0; chan < cluster.get_soc_descriptor(0).get_num_dram_channels(); chan++) {
        CoreCoord core = cluster.get_soc_descriptor(0).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
        test_utils::read_data_from_device(cluster, readback_membar_vec, 0, core, 0, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers were correctly initialized on all DRAM
        readback_membar_vec = {};
    }

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::ETH)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0),
            187);  // Ensure that memory barriers were correctly initialized on all ethernet cores
        readback_membar_vec = {};
    }

    // Launch 2 thread accessing different locations of L1 and using memory barrier between write and read
    // Ensure now RAW race and membars are thread safe.
    std::vector<uint32_t> vec1(2560);
    std::vector<uint32_t> vec2(2560);
    std::vector<uint32_t> zeros(2560, 0);

    for (int i = 0; i < vec1.size(); i++) {
        vec1.at(i) = i;
    }
    for (int i = 0; i < vec2.size(); i++) {
        vec2.at(i) = vec1.size() + i;
    }
    std::thread th1 = std::thread([&] {
        std::uint32_t address = base_addr;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                std::vector<uint32_t> readback_vec = {};
                cluster.write_to_device(vec1.data(), vec1.size() * sizeof(std::uint32_t), 0, core, address);
                cluster.l1_membar(0, {core});
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 4 * vec1.size());
                ASSERT_EQ(readback_vec, vec1);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), 0, core, address);
                readback_vec = {};
            }
        }
    });

    std::thread th2 = std::thread([&] {
        std::uint32_t address = base_addr + vec1.size() * 4;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                std::vector<uint32_t> readback_vec = {};
                cluster.write_to_device(vec2.data(), vec2.size() * sizeof(std::uint32_t), 0, core, address);
                cluster.l1_membar(0, {core});
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 4 * vec2.size());
                ASSERT_EQ(readback_vec, vec2);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), 0, core, address);
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, l1_mem::address_map::L1_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers end up in the correct sate for workers
        readback_membar_vec = {};
    }

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::ETH)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0),
            187);  // Ensure that memory barriers end up in the correct sate for ethernet cores
        readback_membar_vec = {};
    }
    cluster.close_device();
}

// Detection probe for issue #2735: Write->Write NoC ordering on Blackhole.
//
// On Blackhole the cached data-path TLBs run with dynamic VC (static_vc = 0) and
// Default/Relaxed ordering. Per the ISA, in this mode two writes to the same
// destination can reorder on the NoC once they leave the PCIe-tile NIU; ordering is
// only guaranteed when static_vc is set. This test tries to *observe* such a reorder
// from the host with a message-passing pattern on a single Tensix core:
//
//   Writer (window W): fill DATA[..] = k ; sfence ; FLAG = k   (k monotonically ++)
//   Reader (window R): f = read(FLAG) ; tail = read(DATA_last_word)
//
// If ordering holds, DATA(k) lands before FLAG=k, so any observed FLAG value f
// implies the DATA tail is already >= f. A sample with tail < f means FLAG overtook
// DATA -> a Write->Write reorder was observed.
//
// Writer and reader MUST use separate windows (different AXI ids). A shared window is
// one AXI id, and the tile then enforces Default-mode Write->Read ordering, which
// would make the reader's DATA read wait for the writer's DATA write and mask the
// reorder. Blocking MMIO reads already order the reader's own two reads (FLAG then
// tail), so tail reflects state at-or-after the FLAG observation -> no false positives.
//
// One-sided: an observed inversion proves the ordering is broken today; zero
// inversions does NOT prove it is safe (the HW may keep same-src/same-dst writes
// ordered in practice). Run on silicon with today's dynamic-VC config to see whether
// it fires; after the direction-split static_vc fix it must report zero.
TEST(ClusterBH, WriteWriteOrderingProbe) {
    if (!kmd_supports_tlb_alloc()) {
        GTEST_SKIP() << "Requires KMD >= 1.34 for TLB allocation ioctls.";
    }

    constexpr uint32_t kDataWords = 8 * 1024;  // 32 KiB payload -> multi-packet write, tail lands last
    constexpr uint32_t kDataBytes = kDataWords * sizeof(uint32_t);
    constexpr uint32_t kIterations = 100000;
    constexpr uint64_t kTwoMb = 1ull << 21;

    const uint32_t data_off = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    const uint32_t flag_off = data_off + kDataBytes;                 // FLAG immediately after DATA
    const uint32_t tail_off = flag_off - sizeof(uint32_t);           // last word of DATA
    ASSERT_LT(flag_off + sizeof(uint32_t), l1_mem::address_map::MAX_SIZE) << "DATA + FLAG must fit within L1.";

    auto cluster_ptr = test_utils::make_default_test_cluster();
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);

    const ChipId chip = *cluster.get_target_mmio_device_ids().begin();
    PCIDevice* pci = cluster.get_tt_device(chip)->get_pci_device();
    const CoreCoord core =
        cluster.get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED).front();

    auto make_window = [&](uint64_t ordering) {
        tlb_data config{};
        config.local_offset = 0;  // window base at tile address 0; offsets below are raw L1 addresses
        config.x_end = core.x;
        config.y_end = core.y;
        config.noc_sel = 0;
        config.mcast = 0;
        config.ordering = ordering;
        config.linked = 0;
        config.static_vc = 0;  // dynamic VC == today's Blackhole data-path behavior (the thing under test)
        return std::make_unique<SiliconTlbWindow>(pci->allocate_tlb(kTwoMb, TlbMapping::WC), config);
    };

    // Separate windows => separate AXI ids => no Write->Read ordering between writer and reader.
    std::unique_ptr<TlbWindow> writer_win = make_window(tlb_data::Relaxed);
    std::unique_ptr<TlbWindow> reader_win = make_window(tlb_data::Relaxed);

    std::atomic<bool> writer_done{false};

    std::thread writer([&] {
        std::vector<uint32_t> data(kDataWords);
        for (uint32_t k = 1; k <= kIterations; ++k) {
            std::fill(data.begin(), data.end(), k);
            writer_win->write_block(data_off, data.data(), kDataBytes);
            tt_driver_atomics::sfence();  // push all of DATA before FLAG (host side)
            writer_win->write32(flag_off, k);
            tt_driver_atomics::sfence();  // make FLAG promptly visible to the reader
        }
        writer_done.store(true, std::memory_order_release);
    });

    uint64_t samples = 0;
    uint64_t inversions = 0;
    uint32_t first_bad_flag = 0;
    uint32_t first_bad_tail = 0;
    std::thread reader([&] {
        while (!writer_done.load(std::memory_order_acquire)) {
            const uint32_t f = reader_win->read32(flag_off);  // blocking MMIO read
            if (f == 0) {
                continue;
            }
            const uint32_t tail = reader_win->read32(tail_off);  // issued only after f returned
            ++samples;
            if (tail < f) {  // FLAG=f visible but DATA(f) tail not yet -> Write->Write reorder
                ++inversions;
                if (inversions == 1) {
                    first_bad_flag = f;
                    first_bad_tail = tail;
                }
            }
        }
    });

    writer.join();
    reader.join();

    RecordProperty("iterations", std::to_string(kIterations));
    RecordProperty("reader_samples", std::to_string(samples));
    RecordProperty("inversions", std::to_string(inversions));

    EXPECT_EQ(inversions, 0u) << "Observed " << inversions << " Write->Write reorderings out of " << samples
                              << " reader samples (dynamic VC). First: FLAG=" << first_bad_flag
                              << " but DATA tail=" << first_bad_tail << ". Reproduces issue #2735; enabling "
                              << "direction-split static_vc should drive this to zero.";

    cluster.close_device();
}

// Regression test for the no-double-store property of write_to_device on TLB-mapped
// device memory. Each iteration issues one DWORD write_to_device and checks that the
// NIU posted-write counter advances by exactly one — i.e. that we did not emit two
// PCIe writes for the same address (which is what glibc memcpy's overlapping tail
// stores would do, and which corrupts data on device memory).
//
// Assumes single-writer single-process: nothing else on the host or device is writing
// to this chip while the test runs. Compares the NIU counter delta against a
// host-side counter, so any concurrent writer invalidates the assertion.
TEST(ClusterBH, WriteCountMatchesPostedWrites) {
    // NOC register that counts the number of posted write requests received by a core.
    constexpr uint64_t NIU_SLV_POSTED_WR_REQ_RECEIVED = 0xffb202e0;
    constexpr uint32_t kNumWrites = 100000;

    auto cluster_ptr = test_utils::make_default_test_cluster();
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);
    test_utils::safe_test_cluster_start(&cluster);

    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto tensix_core = cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX).at(0);

    uint64_t addr = 0;
    uint32_t data = 1;
    uint32_t counter = 0;

    uint32_t base_reg_val;
    cluster.read_from_device_reg(&base_reg_val, chip_id, tensix_core, NIU_SLV_POSTED_WR_REQ_RECEIVED, sizeof(uint32_t));

    for (uint32_t i = 0; i < kNumWrites; i++) {
        cluster.write_to_device(&data, sizeof(data), chip_id, tensix_core, addr);
        counter++;

        uint32_t reg_val;
        cluster.read_from_device_reg(&reg_val, chip_id, tensix_core, NIU_SLV_POSTED_WR_REQ_RECEIVED, sizeof(uint32_t));

        uint32_t diff = reg_val - base_reg_val;

        ASSERT_EQ(counter, diff);

        uint32_t data_check = 0;
        cluster.read_from_device(&data_check, chip_id, tensix_core, addr, sizeof(data));

        EXPECT_EQ(static_cast<uint32_t>(data_check), static_cast<uint32_t>(data));

        data++;
    }
    cluster.close_device();
}

// Verifies that all ETH channels are classified as either active/idle.
TEST(ClusterBH, TotalNumberOfEthCores) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const uint32_t num_eth_cores = cluster->get_soc_descriptor(0).get_cores(CoreType::ETH).size();

    ClusterDescriptor* cluster_desc = cluster->get_cluster_description();
    const uint32_t num_active_channels = cluster_desc->get_active_eth_channels(0).size();
    const uint32_t num_idle_channels = cluster_desc->get_idle_eth_channels(0).size();

    EXPECT_EQ(num_eth_cores, num_active_channels + num_idle_channels);
}

TEST(ClusterBH, PCIECores) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    for (ChipId chip : cluster->get_target_device_ids()) {
        const auto& pcie_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::PCIE);

        EXPECT_EQ(pcie_cores.size(), 1);

        const auto& harvested_pcie_cores = cluster->get_soc_descriptor(chip).get_harvested_cores(CoreType::PCIE);

        EXPECT_EQ(harvested_pcie_cores.size(), 1);

        EXPECT_NE(pcie_cores.at(0).x, harvested_pcie_cores.at(0).x);
    }
}

TEST(ClusterBH, L2CPUCores) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    for (ChipId chip : cluster->get_target_device_ids()) {
        const auto& l2cpu_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::L2CPU);
        const auto& harvested_l2cpu_cores = cluster->get_soc_descriptor(chip).get_harvested_cores(CoreType::L2CPU);

        EXPECT_LE(harvested_l2cpu_cores.size(), 2);
        EXPECT_EQ(l2cpu_cores.size() + harvested_l2cpu_cores.size(), 4);
    }
}

// Unlike WH, which can support both untranslated and translated coordinate spaces, on BH these spaces are overlapping.
TEST(ClusterBH, VirtualCoordinateBroadcast) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly, and that
    // a broadcast targeting one core type does not leak writes to the other.
    // Blackhole has no ERISC firmware broadcast, so this exercises the SW fallback in broadcast_write_to_cluster.
    auto cluster_ptr = test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    Cluster& cluster = *cluster_ptr;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    test_utils::safe_test_cluster_start(&cluster);

    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    // For BH the broadcast falls back to a per-chip SW loop. Exclude DRAM translated cols (17, 18) plus a couple of
    // tensix rows/cols (translated cols 1..7, 10..16; rows 2..11) for selective filtering.
    std::set<uint32_t> rows_to_exclude = {4, 6};
    std::set<uint32_t> cols_to_exclude = {3, 11, 17, 18};
    // Exclude all tensix translated columns (1..7, 10..16) so only DRAM (translated cols 17..18) receives the
    // broadcast.
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {
        24, 25};  // Exclude ETH and PCIE rows for the DRAM broadcast
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16};

    // Pre-zero tensix L1 and DRAM at the test address so the "not written" assertions have a known baseline.
    std::vector<uint32_t> initial_zeros(broadcast_sizes.back(), 0);
    for (auto chip_id : cluster.get_target_device_ids()) {
        for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
        for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
            const CoreCoord core =
                cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
    }
    cluster.wait_for_non_mmio_flush();

    for (const auto& size : broadcast_sizes) {
        std::vector<uint32_t> vector_to_write(size);
        std::vector<uint32_t> zeros(size);
        std::vector<uint32_t> readback_vec = {};
        for (int i = 0; i < size; i++) {
            vector_to_write[i] = i;
            zeros[i] = 0;
        }
        // Broadcast to Tensix.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude, true);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            // Tensix cores received the broadcast; zero them out.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from chip " << chip_id << " core " << core.str()
                    << " does not match what was broadcasted for size " << size;
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
                readback_vec = {};
            }
            // DRAM cores must NOT have been written by the tensix broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "DRAM core " << chip_id << " " << core.str()
                                               << " was modified by tensix broadcast for size " << size;
                readback_vec = {};
            }
        }
        cluster.wait_for_non_mmio_flush();

        // Broadcast to DRAM.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast,
            true);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            // DRAM cores received the broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from DRAM core " << chip_id << " " << core.str()
                    << " does not match what was broadcasted for size " << size;
                readback_vec = {};
            }
            // Tensix cores must NOT have been written by the DRAM broadcast.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "Tensix core " << chip_id << " " << core.str()
                                               << " was modified by DRAM broadcast for size " << size;
                readback_vec = {};
            }
            // Zero DRAM so the next iteration starts clean.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
            }
        }
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}
