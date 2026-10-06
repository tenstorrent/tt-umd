// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Reading the package's configuration off the device instead of compiling it in.
//
// A Grendel package presents one PCIe function whichever chiplets it is built from, so 0xfeed says
// nothing about what is behind it. The inbound translation tables do, and bring-up has programmed
// them before the host sees the device.
//
// This is checked against an independent measurement rather than against itself: tt-kmd's
// tools/keraunos_tlb dumped the same tables on keraunos/pcie_5hsio (2026-10-06, zrun.12) and
// reported four regions plus the driver's own entry. If discovery and that tool disagree, one of
// them is wrong about the hardware, which is the point of asserting the tool's numbers here rather
// than whatever discovery happens to return.
//
// Read only: every access is a 4-byte read of TLBCFG through the Keraunos-local aperture, the same
// path the tool uses, and the addresses are the ones it already reads safely.

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "tt-umd/arch/keraunos_tlb_discovery.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/protocol/kmd_scalar_noc_access.hpp"
#include "tt-umd/types/arch.hpp"

using namespace tt::umd;

namespace {

class KeraunosTlbDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int device_id : PCIDevice::enumerate_devices()) {
            auto pci_device = std::make_unique<PCIDevice>(device_id);
            if (pci_device->get_arch() == tt::ARCH::QUASAR) {
                access_ = std::make_unique<KmdScalarNocAccess>(std::move(pci_device));
                return;
            }
        }
        GTEST_SKIP() << "No Grendel device is bound to the driver.";
    }

    std::unique_ptr<KmdScalarNocAccess> access_;
};

}  // namespace

// Without inbound translation every window below is meaningless, so this is the first thing to
// establish and the reason the register is read at all.
TEST_F(KeraunosTlbDiscoveryTest, ReportsInboundTranslationEnabled) {
    const auto config = keraunos_tlb::discover(*access_, keraunos_tlb::APPIN0);

    EXPECT_TRUE(config.inbound_enabled()) << "access_ctrl = 0x" << std::hex << config.access_ctrl;
    EXPECT_EQ(config.system_status & 1u, 1u) << "system not ready";
}

// The four regions keraunos_tlb reported, in its order. A package with different chiplets would
// legitimately differ here -- that is the point of discovering rather than asserting a fixed map --
// so a failure means either the model changed or discovery is misreading the table.
TEST_F(KeraunosTlbDiscoveryTest, FindsTheRegionsTheReferenceToolReported) {
    const auto config = keraunos_tlb::discover(*access_, keraunos_tlb::APPIN0);

    struct Expected {
        uint32_t first_entry;
        uint32_t entry_count;
        uint64_t target_base;
    };

    constexpr Expected expected[] = {
        {0, 64, 0x10000000000ULL},   // NEO L1 window, 1 GiB, one 16 MiB slot per tile
        {64, 4, 0x1280000000ULL},    // Mimir CCE
        {68, 32, 0x1200000000ULL},   // the Keraunos chiplet itself
        {100, 64, 0x1300000000ULL},  // Mimir config
    };

    for (const auto& want : expected) {
        SCOPED_TRACE(
            ::testing::Message() << "entries " << want.first_entry << "-" << want.first_entry + want.entry_count - 1);
        const keraunos_tlb::Region* region = config.find_region(want.target_base);
        ASSERT_NE(region, nullptr) << "no region covers 0x" << std::hex << want.target_base;
        EXPECT_EQ(region->first_entry, want.first_entry);
        EXPECT_EQ(region->entry_count, want.entry_count);
        EXPECT_EQ(region->target_base, want.target_base);
        EXPECT_EQ(region->bar_offset, uint64_t{want.first_entry} << 24);
    }
}

// The kernel reserves an entry past the end of bring-up's run and repoints it per access, so it is
// live but its target is whatever the last access used. Finding it valid is what confirms the
// driver has its window; asserting where it points would be asserting our own last read.
TEST_F(KeraunosTlbDiscoveryTest, SeesTheDriversReservedEntry) {
    const auto config = keraunos_tlb::discover(*access_, keraunos_tlb::APPIN0);

    constexpr uint32_t KERNEL_ENTRY = 164;
    const auto found = std::find_if(
        config.windows.begin(), config.windows.end(), [](const auto& window) { return window.entry == KERNEL_ENTRY; });

    EXPECT_NE(found, config.windows.end()) << "entry 164 is not programmed; the driver has no scalar window";
}

// BAR4's 8 GiB windows are where GDDR would be, and nothing has read them back yet. This records
// what is there rather than asserting a number nobody has measured.
TEST_F(KeraunosTlbDiscoveryTest, ReportsWhatTheLargeWindowTableHolds) {
    const auto config = keraunos_tlb::discover(*access_, keraunos_tlb::APPIN1);

    RecordProperty("appin1_valid_windows", std::to_string(config.windows.size()));
    for (const auto& region : config.regions) {
        RecordProperty(
            "appin1_region_" + std::to_string(region.first_entry),
            "entries " + std::to_string(region.entry_count) + " -> 0x" + fmt::format("{:x}", region.target_base));
    }
    SUCCEED() << config.windows.size() << " valid APPIN1 windows";
}
