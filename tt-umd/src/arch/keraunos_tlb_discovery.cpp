// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/keraunos_tlb_discovery.hpp"

#include <fmt/format.h>

#include "tt-umd/tt_device/protocol/kmd_scalar_noc_access.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd::keraunos_tlb {

namespace {

/** One 32-bit word of TLBCFG, in the Keraunos-local space the KLA flag selects. */
uint32_t read_word(KmdScalarNocAccess& access, uint64_t addr) {
    uint64_t value = 0;
    access.read(addr, &value, sizeof(uint32_t), KmdScalarNocAccess::FLAG_LOCAL_ADDRESS);
    return static_cast<uint32_t>(value);
}

/**
 * An entry's target address and whether it is live.
 *
 * The low bit of the first word is the valid flag and is not part of the address, which is why it
 * is masked off rather than shifted away: the address is already 16 MB aligned, so the bit sits in
 * space the address never uses.
 */
std::pair<uint64_t, bool> read_entry(KmdScalarNocAccess& access, const TableGeometry& table, uint32_t entry) {
    const uint64_t base = TLBCFG_BASE + table.tlbcfg_offset + uint64_t{entry} * ENTRY_STRIDE;
    const uint32_t low = read_word(access, base);
    const uint32_t high = read_word(access, base + sizeof(uint32_t));

    const uint64_t target = (uint64_t{high} << 32) | (low & ~uint32_t{1});
    return {target, (low & 1u) != 0};
}

}  // namespace

const Region* Configuration::find_region(uint64_t addr) const {
    for (const Region& region : regions) {
        if (region.contains(addr)) {
            return &region;
        }
    }
    return nullptr;
}

Configuration discover(KmdScalarNocAccess& access, const TableGeometry& table) {
    Configuration config;
    config.access_ctrl = read_word(access, ACCESS_CTRL);
    config.system_status = read_word(access, SYSTEM_STATUS);

    for (uint32_t entry = 0; entry < table.entry_count; entry++) {
        const auto [target, valid] = read_entry(access, table, entry);
        if (!valid) {
            continue;
        }
        config.windows.push_back({entry, target, uint64_t{entry} * table.window_size, table.window_size});
    }

    // Coalesce a run of entries that steps through its targets at the window size into the one
    // region it forms, which is how bring-up programs a chiplet: consecutive entries, consecutive
    // targets. A run that steps by anything else is two regions that happen to be adjacent.
    for (const Window& window : config.windows) {
        const bool extends = !config.regions.empty() &&
                             config.regions.back().first_entry + config.regions.back().entry_count == window.entry &&
                             config.regions.back().target_base + config.regions.back().size == window.target;
        if (extends) {
            config.regions.back().entry_count++;
            config.regions.back().size += window.size;
        } else {
            config.regions.push_back({window.entry, 1, window.target, window.bar_offset, window.size});
        }
    }

    return config;
}

}  // namespace tt::umd::keraunos_tlb
