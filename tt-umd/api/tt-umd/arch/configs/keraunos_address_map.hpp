// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

#include "tt-umd/utils/error.hpp"

namespace tt::umd::keraunos {

/**
 * The statically mapped regions of a Keraunos package, as BAR0 presents them.
 *
 * Transcribed from "Grendel PCIe - Running in Emulation" in the SIVAL space
 * (https://tenstorrent.atlassian.net/wiki/spaces/SIVAL/pages/2027389218), whose table gives the
 * BAR0 TLB entry range, BAR0 offset, system physical base and size of each region.
 *
 * Bring-up programs these inbound TLB entries before the host ever sees the device, so they are
 * not UMD's to allocate or reprogram -- this is a description of what is already there. The kernel
 * driver reserves an entry well past the end of this run for its own scalar accesses.
 *
 * These values describe the Keraunos combination only. Another Grendel package maps its own
 * chiplets, which is why this is data beside the architecture rather than constants inside it.
 *
 * It is a snapshot, not a contract. This is what bring-up programs on the emulation model today --
 * keraunos/pcie_5hsio, release 26ww23, read 2026-10-06 -- and nothing obliges it to stay put. A
 * later bring-up can move a region or add one, silicon need not place them where the model does,
 * and a different package maps different chiplets. Expect to update this file rather than to trust
 * it.
 *
 * A stale table does not fail loudly, which is the reason to say so here. find_region() would go on
 * blessing an address that no longer reaches anything, and on the emulator an access to an
 * unmodelled region hangs the machine for everyone on it -- the exact outcome this table exists to
 * prevent. tt-kmd's tools/keraunos_addrmap.h describes the same hardware from the driver side and
 * is what to check this against; where either disagrees with the device, the device is right.
 *
 * READ BACK OFF A DEVICE 2026-10-06, and these rows are an idealization of what is there.
 * tt-kmd's tools/keraunos_tlb and tools/keraunos_pcie_remap dumped the live tables on
 * keraunos/pcie_5hsio. The entry range and bases above are confirmed exactly -- entries 68-99 at
 * BAR0 0x4400_0000 covering SPA 0x12_0000_0000 -- but the regions are not uniformly remapped.
 * 144 MiB of the 512 MiB window has no SPA->KLA entry and, per the tool, "passes through as SPA
 * (NoC)":
 *
 *   PCIe MMR        0x12_1800_0000 + 64 MiB   the whole row; nothing maps it
 *   SMN MMR / D2D   0x12_1D00_0000 + 48 MiB   only the first 16 MiB of the row is mapped
 *   HSIO 1..4       8 MiB each                the TL1 SRAM hole inside each 64 MiB row
 *
 * find_region() returns a region for every one of those addresses, because it only range-checks
 * against the rows below. Treat a hit as "inside a row the package nominally covers", not as
 * "this reaches a remapped target". The eleven entries the dump warns about -- 78, 82, 86, 90,
 * 92-95, 97-99 -- are exactly the ones these holes fall in.
 *
 * The same dump shows BAR0 windows package space this table says nothing about: entries 0-63 onto
 * system SRAM at SPA 0x100_0000_0000, 64-67 onto Mimir CCE at 0x12_8000_0000, and 100-163 onto
 * Mimir config at 0x13_0000_0000. They are real and reachable; they are simply outside what a
 * Keraunos-chiplet region table set out to describe.
 */

/** Each inbound TLB entry covers 16 MB of BAR0, so an entry index and a BAR0 offset are the same fact. */
inline constexpr uint32_t TLB_WINDOW_SHIFT = 24;
inline constexpr uint64_t TLB_WINDOW_SIZE = uint64_t{1} << TLB_WINDOW_SHIFT;

/** Base of the system physical range the package occupies. */
inline constexpr uint64_t SPA_BASE = 0x1200000000ULL;

/** One region, stated the way the table states it. */
struct SpaRegion {
    const char* name;
    /** First inbound TLB entry covering the region. */
    uint32_t first_tlb_entry;
    /** How many consecutive entries it spans. */
    uint32_t tlb_entry_count;
    /** Offset into BAR0 the region begins at. */
    uint64_t bar0_offset;
    /** System physical address the region begins at. */
    uint64_t spa_base;
    /** Extent in bytes. */
    uint64_t size;

    /** Whether @p addr falls inside this region. */
    constexpr bool contains(uint64_t addr) const { return addr >= spa_base && addr - spa_base < size; }
};

inline constexpr SpaRegion SPA_REGIONS[] = {
    {"SEP", 68, 2, 0x44000000, 0x1200000000ULL, 32 * 1024 * 1024ULL},
    {"SMC", 70, 2, 0x46000000, 0x1202000000ULL, 32 * 1024 * 1024ULL},
    {"HSIO 0", 72, 4, 0x48000000, 0x1204000000ULL, 64 * 1024 * 1024ULL},
    {"HSIO 1", 76, 4, 0x4C000000, 0x1208000000ULL, 64 * 1024 * 1024ULL},
    {"HSIO 2", 80, 4, 0x50000000, 0x120C000000ULL, 64 * 1024 * 1024ULL},
    {"HSIO 3", 84, 4, 0x54000000, 0x1210000000ULL, 64 * 1024 * 1024ULL},
    {"HSIO 4", 88, 4, 0x58000000, 0x1214000000ULL, 64 * 1024 * 1024ULL},
    {"PCIe MMR", 92, 4, 0x5C000000, 0x1218000000ULL, 64 * 1024 * 1024ULL},
    {"SMN MMR / D2D", 96, 4, 0x60000000, 0x121C000000ULL, 64 * 1024 * 1024ULL},
};

/**
 * The region @p addr falls in, or nullptr if it falls outside the package's range.
 *
 * On the emulator an access to an unmodelled address can hang the machine rather than fail, so
 * knowing whether an address reaches anything is worth checking before issuing it.
 */
inline constexpr const SpaRegion* find_region(uint64_t addr) {
    for (const SpaRegion& region : SPA_REGIONS) {
        if (region.contains(addr)) {
            return &region;
        }
    }
    return nullptr;
}

/**
 * Where host memory appears to the device.
 *
 * The inbound regions above are the host reaching into the package; this is the reverse. The
 * fabric routes this window to the PCIe outbound port, so a device-side access here becomes a TLP
 * to the host. The package-level form of the same window is 0x6_..., which the Mimir and Quasar
 * translation tables rebase to this before it arrives, so this is the value to use from inside
 * Keraunos. It is the same constant the driver's tools call KERAUNOS_HOST_WINDOW.
 */
inline constexpr uint64_t HOST_WINDOW_SPA = 0x2000000000000ULL;

/** Outbound translation selects its entry from bits [47:44], so a host address must fit 48 bits. */
inline constexpr uint32_t HOST_ADDRESS_BITS = 48;

/**
 * The device-side address that reaches host address @p host_addr, which is what PIN_PAGES
 * returned: an IOVA under an IOMMU, a physical address otherwise.
 *
 * @throws error::RuntimeError if the host address is wider than the window can carry. Truncating
 *         it would reach a different page rather than fail, so it is refused.
 */
inline uint64_t host_window_address(uint64_t host_addr) {
    UMD_ASSERT(
        host_addr < (uint64_t{1} << HOST_ADDRESS_BITS),
        error::RuntimeError,
        fmt::format(
            "Host address 0x{:x} is wider than the {} bits the outbound window can carry.",
            host_addr,
            HOST_ADDRESS_BITS));

    return HOST_WINDOW_SPA + host_addr;
}

}  // namespace tt::umd::keraunos
