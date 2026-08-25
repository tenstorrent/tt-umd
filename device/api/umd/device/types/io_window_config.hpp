// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

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
 * @brief Type-safe transaction attributes for an IoWindow target.
 */
enum class WindowFlags : uint32_t {
    None = 0,
    Atomic = 1 << 0,  ///< Issue transaction as atomic on the target interconnect.
    Snoop = 1 << 1,   ///< Mark transaction as snoopable for cache coherency.
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
 * @brief Device-side target for an IoWindow: core, address, optional NOC, and transaction flags.
 */
struct TargetIoWindowConfig {
    tt_xy_pair core_start;  ///< Target core, or upper-left corner of a multicast grid.
    std::optional<tt_xy_pair> core_end =
        std::nullopt;                         ///< Lower-right corner of a multicast grid, or nullopt for unicast.
    uint64_t addr;                            ///< Destination address on the target core(s).
    std::optional<NocId> noc = std::nullopt;  ///< Optional routing selection.
    WindowFlags flags = WindowFlags::None;    ///< Transaction attributes.
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
