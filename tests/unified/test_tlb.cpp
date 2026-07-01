// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/mmio_timeout_config.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"

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
