// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
// TODO: To be removed once this is fixed in tt_metal.
#include <deque>

// Types in this file can be used without using the driver, hence they aren't in tt::umd namespace.
namespace tt {

struct xy_pair {
    constexpr xy_pair() : x{}, y{} {}

    constexpr xy_pair(std::size_t x, std::size_t y) : x(x), y(y) {}

    std::size_t x;
    std::size_t y;

    std::string str() const;
};

constexpr bool operator==(const xy_pair &a, const xy_pair &b) { return a.x == b.x && a.y == b.y; }

constexpr bool operator!=(const xy_pair &a, const xy_pair &b) { return !(a == b); }

constexpr bool operator<(const xy_pair &left, const xy_pair &right) {
    return (left.x < right.x || (left.x == right.x && left.y < right.y));
}

struct cxy_pair : public xy_pair {
    cxy_pair() = default;

    cxy_pair(std::size_t ichip, xy_pair pair) : xy_pair(pair.x, pair.y), chip(ichip) {}

    cxy_pair(std::size_t ichip, std::size_t x, std::size_t y) : xy_pair(x, y), chip(ichip) {}

    std::size_t chip;

    std::string str() const;
};

constexpr bool operator==(const cxy_pair &a, const cxy_pair &b) { return a.x == b.x && a.y == b.y && a.chip == b.chip; }

constexpr bool operator!=(const cxy_pair &a, const cxy_pair &b) { return !(a == b); }

constexpr bool operator<(const cxy_pair &left, const cxy_pair &right) {
    return (
        left.chip < right.chip || (left.chip == right.chip && left.x < right.x) ||
        (left.chip == right.chip && left.x == right.x && left.y < right.y));
}

}  // namespace tt

// These are convenience typedefs for the xy_pair and cxy_pair types.
using tt_xy_pair = tt::xy_pair;
using tt_cxy_pair = tt::cxy_pair;

namespace std {
template <>
struct hash<tt::xy_pair> {
    std::size_t operator()(tt::xy_pair const &o) const {
        std::size_t seed = 0;
        seed = std::hash<std::size_t>()(o.x) ^ std::hash<std::size_t>()(o.y) << 1;
        return seed;
    }
};
}  // namespace std

namespace std {
template <>
struct hash<tt::cxy_pair> {
    std::size_t operator()(tt::cxy_pair const &o) const {
        std::size_t seed = 0;
        seed = std::hash<std::size_t>()(o.chip) ^ (std::hash<std::size_t>()(o.x) << 1) ^
               (std::hash<std::size_t>()(o.y) << 2);
        return seed;
    }
};
}  // namespace std
