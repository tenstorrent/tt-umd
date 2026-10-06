// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace tt::umd {

class KmdScalarNocAccess;

/**
 * What a Grendel package maps, read back off the device rather than transcribed.
 *
 * A package presents one PCIe function whichever chiplets it is built from, so the device id says
 * nothing about what is behind it: a Keraunos-only build and a full Keraunos + Quasar + Mimir
 * build are the same 0xfeed. The inbound translation tables are the difference, and bring-up has
 * already programmed them by the time the host sees the device, so reading them is the one way to
 * learn the configuration at runtime rather than compiling a guess in.
 *
 * Reached through the driver's scalar accesses with the Keraunos-local flag set, the way tt-kmd's
 * tools/keraunos_tlb does. That path needs only the device node -- no BAR2 mapping, which the
 * driver restricts to CAP_SYS_ADMIN.
 */
namespace keraunos_tlb {

/** TLBCFG, in the Keraunos-local address space the KLA flag selects. */
inline constexpr uint64_t TLBCFG_BASE = 0x18040000ULL;
inline constexpr uint64_t ACCESS_CTRL = TLBCFG_BASE + 0xfff8ULL;
inline constexpr uint64_t SYSTEM_STATUS = TLBCFG_BASE + 0xfffcULL;

/** Bit 16 of access_ctrl; with it clear the inbound windows below mean nothing. */
inline constexpr uint32_t ACCESS_CTRL_INBOUND_APP_ENABLED = 1u << 16;

/** One inbound table: where its entries live in TLBCFG, how many, and what each one spans. */
struct TableGeometry {
    const char* name;
    uint32_t tlbcfg_offset;
    uint32_t entry_count;
    uint64_t window_size;
};

/** The two inbound APP tables. Entry stride is 0x40 bytes; the address is two 32-bit words. */
inline constexpr TableGeometry APPIN0{"APPIN0", 0x4000, 256, 0x01000000ULL};  // BAR0, 16 MiB each
inline constexpr TableGeometry APPIN1{"APPIN1", 0x8000, 64, 0x200000000ULL};  // BAR4, 8 GiB each

inline constexpr uint32_t ENTRY_STRIDE = 0x40;

/** One programmed window: the entry, what it reaches, and where it sits in the BAR. */
struct Window {
    uint32_t entry;
    uint64_t target;
    uint64_t bar_offset;
    uint64_t size;
};

/** Consecutive entries reaching consecutive targets, reported as the one region they form. */
struct Region {
    uint32_t first_entry;
    uint32_t entry_count;
    uint64_t target_base;
    uint64_t bar_offset;
    uint64_t size;

    constexpr bool contains(uint64_t addr) const { return addr >= target_base && addr - target_base < size; }
};

/** What one read of the device found. */
struct Configuration {
    uint32_t access_ctrl = 0;
    uint32_t system_status = 0;
    std::vector<Window> windows;
    std::vector<Region> regions;

    bool inbound_enabled() const { return (access_ctrl & ACCESS_CTRL_INBOUND_APP_ENABLED) != 0; }

    /** The region @p addr falls in, or nullptr. This is the measured answer, not a table's claim. */
    const Region* find_region(uint64_t addr) const;
};

/**
 * Read @p table back off the device.
 *
 * Costs two scalar accesses per entry -- 512 for APPIN0 -- each a driver round trip that
 * reprograms the kernel's window, so this is an open-time operation whose result is worth keeping.
 *
 * @throws error::RuntimeError if a read fails.
 */
Configuration discover(KmdScalarNocAccess& access, const TableGeometry& table);

}  // namespace keraunos_tlb
}  // namespace tt::umd
