// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <string_view>

namespace tt {

/**
 * @brief Raw (x, y) coordinate pair on the NOC grid.
 */
struct xy_pair {
    constexpr xy_pair() = default;

    constexpr xy_pair(std::size_t x, std::size_t y) : x(x), y(y) {}

    std::size_t x = 0;
    std::size_t y = 0;

    std::string str() const;
};

constexpr bool operator==(const xy_pair &a, const xy_pair &b) { return a.x == b.x && a.y == b.y; }

constexpr bool operator!=(const xy_pair &a, const xy_pair &b) { return !(a == b); }

constexpr bool operator<(const xy_pair &left, const xy_pair &right) {
    return (left.x < right.x || (left.x == right.x && left.y < right.y));
}

}  // namespace tt

using tt_xy_pair = tt::xy_pair;  ///< Convenience alias.

namespace std {
template <>
struct hash<tt::xy_pair> {
    std::size_t operator()(tt::xy_pair const &o) const {
        std::size_t seed = 0;
        seed ^= std::hash<std::size_t>()(o.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::size_t>()(o.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

}  // namespace std
