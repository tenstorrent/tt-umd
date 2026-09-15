// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "umd/device/coordinates/att/att_window.hpp"

namespace tt::umd::att {

/**
 * A pointer and a count over a transcribed table, deduced from the array so no length is spelled by
 * hand. Stands in for std::span, which UMD cannot use until C++20.
 */
template <typename T>
struct Table {
    const T* data = nullptr;
    uint32_t count = 0;

    constexpr Table() = default;

    template <size_t N>
    constexpr Table(const T (&array)[N]) : data(array), count(N) {}

    constexpr const T& operator[](uint32_t index) const { return data[index]; }

    constexpr uint32_t size() const { return count; }

    constexpr bool empty() const { return count == 0; }
};

/** The role a window plays in a map. Count doubles as the window array extent. */
enum class WindowClass : uint8_t {
    Worker = 0,
    Dram,
    FullTile,

    Count,
};

inline constexpr size_t WINDOW_CLASS_COUNT = static_cast<size_t>(WindowClass::Count);

/** Marks an endpoint table row the map leaves unprogrammed. No core can carry this coordinate. */
inline constexpr uint16_t ENDPOINT_UNPOPULATED = 0xffff;

/**
 * One product's ATT map, as pure data. Every field is transcribed from the generated map the
 * firmware programs; the resolver is the only code that interprets it, so supporting another
 * product is adding data rather than adding a branch.
 */
struct MapData {
    /** Window per role, indexed by WindowClass. */
    std::array<Window, WINDOW_CLASS_COUNT> windows;

    /**
     * Each window's endpoint table, one packed (y << 6) | x word per selector, in the POR package
     * frame and laid out as the hardware register file holds it. Slot order follows the POR
     * endpoint table rather than the mesh geometry, so nothing but the table can say which slot a
     * core occupies.
     */
    std::array<Table<uint16_t>, WINDOW_CLASS_COUNT> endpoint_words;

    /** Added to a soc descriptor coordinate to reach the POR package frame the tables use. */
    uint32_t package_offset_x;
    uint32_t package_offset_y;
};

}  // namespace tt::umd::att
