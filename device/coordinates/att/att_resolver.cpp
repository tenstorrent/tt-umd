// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/coordinates/att/att_resolver.hpp"

#include <fmt/format.h>

#include "umd/device/utils/error.hpp"

namespace tt::umd::att {

namespace {

/** Endpoint tables hold coordinates the way the hardware register does. */
constexpr uint16_t pack_coord(uint32_t x, uint32_t y) { return static_cast<uint16_t>((y << 6) | x); }

WindowClass window_class_for(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
        case CoreType::WORKER:
            return WindowClass::Worker;
        case CoreType::DRAM:
            return WindowClass::Dram;
        // Routers, dispatch, ARC, PCIe and security cores are all reached through their per-tile
        // config aperture.
        default:
            return WindowClass::FullTile;
    }
}

}  // namespace

Resolver::Resolver(const MapData& map) : map_(map) {
    for (size_t window_class = 0; window_class < WINDOW_CLASS_COUNT; ++window_class) {
        const Window& window = map_.windows[window_class];
        const Table<uint16_t>& words = map_.endpoint_words[window_class];

        UMD_ASSERT(
            words.size() <= window.selector_limit(),
            error::RuntimeError,
            fmt::format(
                "ATT window {} declares {} endpoint rows but its selector field only reaches {}.",
                window_class,
                words.size(),
                window.selector_limit()));

        for (uint32_t selector = 0; selector < words.size(); ++selector) {
            if (words[selector] == ENDPOINT_UNPOPULATED) {
                continue;
            }
            const bool inserted = selectors_[window_class].emplace(words[selector], selector).second;
            UMD_ASSERT(
                inserted,
                error::RuntimeError,
                fmt::format(
                    "ATT window {} names core ({}, {}) in more than one endpoint row, so a lookup by "
                    "coordinate cannot say which slot was meant.",
                    window_class,
                    words[selector] & 0x3f,
                    words[selector] >> 6));
        }
    }
}

uint64_t Resolver::resolve(tt_xy_pair core, CoreType core_type, uint64_t offset, uint64_t size) const {
    const auto x = static_cast<uint32_t>(core.x);
    const auto y = static_cast<uint32_t>(core.y);
    const WindowClass window_class = window_class_for(core_type);

    const auto& selectors = selectors_[static_cast<size_t>(window_class)];
    const auto entry = selectors.find(pack_coord(x + map_.package_offset_x, y + map_.package_offset_y));
    UMD_ASSERT(
        entry != selectors.end(),
        error::RuntimeError,
        fmt::format("Core ({}, {}) type {} has no endpoint in this ATT map.", x, y, to_str(core_type)));

    const Window& window = map_.windows[static_cast<size_t>(window_class)];
    UMD_ASSERT(
        window.transfer_supported(offset, size),
        error::RuntimeError,
        fmt::format(
            "A {} byte transfer at offset 0x{:x} runs past the 0x{:x} byte slot of core ({}, {}), so it would "
            "reach a different core.",
            size,
            offset,
            window.local_address_limit(),
            x,
            y));

    return window.make_address(entry->second, offset);
}

}  // namespace tt::umd::att
