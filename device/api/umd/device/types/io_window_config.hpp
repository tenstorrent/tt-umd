// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Host memory caching strategy for an IoWindow.
 */
enum class HostMemoryCaching {
    WC,  ///< Write-Combining — bypasses cache, batches small writes into bus bursts. Higher throughput, relaxed
         ///< ordering.
    UC,  ///< Uncacheable — bypasses cache, every access hits hardware immediately. Strict ordering.
};

/**
 * @brief Transaction ordering mode for an IoWindow target.
 *
 * The enumerator values intentionally match the TLB hardware encoding in tlb_data, so that
 * translating between the two is a cast rather than a switch.
 */
enum class IoOrdering : uint8_t {
    Relaxed = 0,  ///< No ordering guarantees between transactions.
    Strict = 1,   ///< Transactions complete in issue order.
    Posted = 2,   ///< Writes are posted; no completion acknowledgement.
};

/**
 * @brief Type-safe transaction attributes for an IoWindow target.
 *
 * Bits 2-3 form the direction field, holding the traffic the mapping carries until it is configured
 * again. A mapping reconfigured before every transfer can commit to one direction, which lets an
 * implementation route reads and writes over separate interconnect resources and so keep writes to a
 * target ordered against each other. Zero — the default — is bidirectional: a mapping that serves
 * both without being configured in between, and so cannot be given a resource of its own.
 * Implementations with nothing to gain from the distinction ignore the field.
 */
enum class WindowFlags : uint32_t {
    None = 0,
    Atomic = 1 << 0,  ///< Issue transaction as atomic on the target interconnect.
    Snoop = 1 << 1,   ///< Mark transaction as snoopable for cache coherency.

    Bidirectional = 0 << 2,   ///< Mapping serves both reads and writes. Default.
    UnicastWrite = 1 << 2,    ///< Mapping serves writes to a single core.
    UnicastRead = 2 << 2,     ///< Mapping serves reads from a single core.
    MulticastWrite = 3 << 2,  ///< Mapping serves writes to a core range.
    DirectionMask = 3 << 2,   ///< Selects the direction field out of a set of flags.
};

constexpr WindowFlags operator|(WindowFlags lhs, WindowFlags rhs) noexcept {
    return static_cast<WindowFlags>(
        static_cast<std::underlying_type_t<WindowFlags>>(lhs) | static_cast<std::underlying_type_t<WindowFlags>>(rhs));
}

constexpr WindowFlags operator&(WindowFlags lhs, WindowFlags rhs) noexcept {
    return static_cast<WindowFlags>(
        static_cast<std::underlying_type_t<WindowFlags>>(lhs) & static_cast<std::underlying_type_t<WindowFlags>>(rhs));
}

constexpr WindowFlags operator~(WindowFlags val) noexcept {
    return static_cast<WindowFlags>(~static_cast<std::underlying_type_t<WindowFlags>>(val));
}

constexpr WindowFlags& operator|=(WindowFlags& lhs, WindowFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr WindowFlags& operator&=(WindowFlags& lhs, WindowFlags rhs) noexcept {
    lhs = lhs & rhs;
    return lhs;
}

/**
 * @brief Device-side target for an IoWindow: core(s), address, optional NOC, and transaction flags.
 */
struct TargetIoWindowConfig {
    CoreCoord core_start;  ///< Target core, or upper-left corner of a multicast grid.
    std::optional<CoreCoord> core_end =
        std::nullopt;                         ///< Lower-right corner of a multicast grid, or nullopt for unicast.
    uint64_t addr;                            ///< Destination address on the target core(s).
    std::optional<NocId> noc = std::nullopt;  ///< Optional routing selection.
    WindowFlags flags = WindowFlags::None;    ///< Transaction attributes, including the direction field.
};

/**
 * @brief Host-side properties for an IoWindow.
 *
 * Controls the host memory caching strategy and requested window size.
 * A size of 0 is valid and delegates the window size selection to the concrete implementation.
 */
struct HostIoWindowConfig {
    HostMemoryCaching mapping = HostMemoryCaching::WC;
    size_t size = 0;
};

}  // namespace tt::umd
