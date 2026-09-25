// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "umd/device/arch/configs/keraunos_address_map.hpp"

using namespace tt::umd;

// The transcribed table states each region three ways -- a TLB entry range, a BAR0 offset and a
// size -- and the hardware derives all three from the entry range. Checking them against each
// other is what catches a typo in the transcription, since a wrong digit breaks the agreement.

TEST(KeraunosAddressMap, Bar0OffsetFollowsFromTheTlbEntry) {
    for (const auto& region : keraunos::SPA_REGIONS) {
        SCOPED_TRACE(region.name);
        EXPECT_EQ(region.bar0_offset, uint64_t{region.first_tlb_entry} << keraunos::TLB_WINDOW_SHIFT);
    }
}

TEST(KeraunosAddressMap, SizeFollowsFromTheEntryCount) {
    for (const auto& region : keraunos::SPA_REGIONS) {
        SCOPED_TRACE(region.name);
        EXPECT_EQ(region.size, uint64_t{region.tlb_entry_count} << keraunos::TLB_WINDOW_SHIFT);
    }
}

// The regions are one contiguous run in both address spaces. A gap would mean a row was dropped in
// transcription, and an overlap would mean two rows claim the same window.
TEST(KeraunosAddressMap, RegionsAreContiguousInBothAddressSpaces) {
    for (size_t i = 1; i < std::size(keraunos::SPA_REGIONS); i++) {
        const auto& previous = keraunos::SPA_REGIONS[i - 1];
        const auto& region = keraunos::SPA_REGIONS[i];
        SCOPED_TRACE(std::string(previous.name) + " -> " + region.name);

        EXPECT_EQ(region.first_tlb_entry, previous.first_tlb_entry + previous.tlb_entry_count);
        EXPECT_EQ(region.bar0_offset, previous.bar0_offset + previous.size);
        EXPECT_EQ(region.spa_base, previous.spa_base + previous.size);
    }
}

// The span is what the kernel driver's own address map calls the Keraunos system physical range,
// so the two descriptions of the same hardware have to agree on where it ends.
TEST(KeraunosAddressMap, SpansTheRangeTheDriverDeclares) {
    constexpr uint64_t DRIVER_SPA_BASE = 0x1200000000ULL;
    constexpr uint64_t DRIVER_SPA_SIZE = 0x20000000ULL;

    EXPECT_EQ(keraunos::SPA_BASE, DRIVER_SPA_BASE);
    EXPECT_EQ(keraunos::SPA_REGIONS[0].spa_base, DRIVER_SPA_BASE);

    const auto& last = keraunos::SPA_REGIONS[std::size(keraunos::SPA_REGIONS) - 1];
    EXPECT_EQ(last.spa_base + last.size, DRIVER_SPA_BASE + DRIVER_SPA_SIZE);
}

// Looking a region up by address is the point of holding the table: it says whether an access can
// reach anything at all, which on the emulator is the difference between a result and a hang.
TEST(KeraunosAddressMap, FindsTheRegionAnAddressFallsIn) {
    const keraunos::SpaRegion* smc = keraunos::find_region(0x1202000000ULL);
    ASSERT_NE(smc, nullptr);
    EXPECT_STREQ(smc->name, "SMC");

    // The last byte of a region still belongs to it; the byte after it belongs to the next one.
    const keraunos::SpaRegion* end_of_smc = keraunos::find_region(0x1202000000ULL + smc->size - 1);
    ASSERT_NE(end_of_smc, nullptr);
    EXPECT_STREQ(end_of_smc->name, "SMC");
}

TEST(KeraunosAddressMap, ReportsNoRegionOutsideTheRange) {
    EXPECT_EQ(keraunos::find_region(keraunos::SPA_BASE - 1), nullptr);
    EXPECT_EQ(keraunos::find_region(0x1220000000ULL), nullptr);
}

// The outbound direction: an address the device uses to reach host memory. The fabric routes this
// window to the PCIe outbound port, where the outbound translation picks its entry out of bits
// [47:44] of the address, so only the low 48 bits of a host address survive the trip.

TEST(KeraunosHostWindow, PlacesAHostAddressInTheOutboundWindow) {
    EXPECT_EQ(keraunos::host_window_address(0), keraunos::HOST_WINDOW_SPA);
    EXPECT_EQ(keraunos::host_window_address(0x1000), keraunos::HOST_WINDOW_SPA + 0x1000);
}

// A wider address cannot be expressed rather than being silently truncated into a window that
// would reach the wrong host page.
TEST(KeraunosHostWindow, RejectsAHostAddressWiderThanTheWindowCanCarry) {
    constexpr uint64_t too_wide = uint64_t{1} << 48;

    EXPECT_THROW(keraunos::host_window_address(too_wide), std::exception);
    EXPECT_NO_THROW(keraunos::host_window_address(too_wide - 1));
}

// The window is the one the driver's own address map names, and it is outside every inbound
// region, so a host address is never mistaken for a target inside the package.
TEST(KeraunosHostWindow, IsOutsideTheInboundRegions) {
    EXPECT_EQ(keraunos::HOST_WINDOW_SPA, 0x2000000000000ULL);
    EXPECT_EQ(keraunos::find_region(keraunos::HOST_WINDOW_SPA), nullptr);
}
