// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd::att {

/** Mask covering the low @p bits bits. */
constexpr uint64_t low_mask(uint32_t bits) { return bits == 64 ? UINT64_MAX : ((uint64_t{1} << bits) - 1); }

/**
 * One window of the Quasar NOC Address Translation Table.
 *
 * Field names match the ATT programming model so that a transcribed configuration can be diffed
 * directly against the mask table it came from.
 */
struct Window {
    /** Window base: the address bits above the wildcard region. */
    uint64_t compare;

    /**
     * Count of low address bits the window comparison ignores, so the window spans 1 << mask_bits
     * bytes. This is the hardware and register-dump convention; the programming guide states the
     * same field inverted, as a count of significant high bits.
     */
    uint8_t mask_bits;

    /** Address bits [endpoint_shift +: endpoint_size] carry the selector. */
    uint8_t endpoint_shift;
    uint8_t endpoint_size;

    /** A selector resolves through endpoint table row endpoint_table_offset + selector. */
    uint16_t endpoint_table_offset;

    /** Whether the ATT rebases the address after matching this window. */
    bool translate_address;

    /** Flat address reaching @p selector_value at core-local byte offset @p local. */
    constexpr uint64_t make_address(uint32_t selector_value, uint64_t local) const {
        return compare | (static_cast<uint64_t>(selector_value) << endpoint_shift) | local;
    }

    /** Selector @p address carries, or 0 for a window that has no selector field. */
    constexpr uint32_t selector(uint64_t address) const {
        return endpoint_size == 0
                   ? 0u
                   : static_cast<uint32_t>((address >> endpoint_shift) & low_mask(endpoint_size));
    }

    /** Endpoint table row @p address resolves through. */
    constexpr uint16_t endpoint_index(uint64_t address) const {
        return static_cast<uint16_t>(endpoint_table_offset + selector(address));
    }

    /** Core-local byte offset @p address carries. */
    constexpr uint64_t local_address(uint64_t address) const { return address & low_mask(local_address_bits()); }

    /**
     * Width of the local-address field. A window with no selector spends every ignored comparison
     * bit on the local address; otherwise the local address ends where the selector begins.
     */
    constexpr uint32_t local_address_bits() const { return endpoint_size == 0 ? mask_bits : endpoint_shift; }

    /** One past the largest core-local offset the window can carry. */
    constexpr uint64_t local_address_limit() const { return uint64_t{1} << local_address_bits(); }

    /** Number of selector values the window supports. */
    constexpr uint32_t selector_limit() const { return endpoint_size == 0 ? 1u : (uint32_t{1} << endpoint_size); }

    /** Whether @p selector_value fits the selector field. */
    constexpr bool selector_supported(uint32_t selector_value) const { return selector_value < selector_limit(); }

    /**
     * Whether a @p size byte transfer at core-local offset @p local stays within one selector's
     * slot. A transfer that runs past it carries into the selector field and reaches a different
     * core instead of failing, so it is rejected here. Zero-size transfers are rejected too.
     */
    constexpr bool transfer_supported(uint64_t local, uint64_t size) const {
        const uint64_t limit = local_address_limit();
        return size != 0 && local < limit && size <= limit - local;
    }
};

}  // namespace tt::umd::att
