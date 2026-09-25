// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

#include <fmt/format.h>

#include "umd/device/utils/error.hpp"

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
