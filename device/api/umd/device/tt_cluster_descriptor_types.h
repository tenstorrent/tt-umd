/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <functional>
#include <tuple>

using chip_id_t = int;
using ethernet_channel_t = int;

struct eth_coord_t {
    int cluster_id;  // This is the same for connected chips.
    int x;
    int y;
    int rack;
    int shelf;

    // in C++20 this should be defined as:
    // constexpr bool operator==(const eth_coord_t &other) const noexcept = default;
    constexpr bool operator==(const eth_coord_t &other) const noexcept {
        return (
            cluster_id == other.cluster_id and x == other.x and y == other.y and rack == other.rack and
            shelf == other.shelf);
    }
};

// Small performant hash combiner taken from boost library.
// Not using boost::hash_combine due to dependency complications.
inline void boost_hash_combine(std::size_t &seed, const int value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std {
template <>
struct hash<eth_coord_t> {
    std::size_t operator()(eth_coord_t const &c) const {
        std::size_t seed = 0;
        boost_hash_combine(seed, c.cluster_id);
        boost_hash_combine(seed, c.x);
        boost_hash_combine(seed, c.y);
        boost_hash_combine(seed, c.rack);
        boost_hash_combine(seed, c.shelf);
        return seed;
    }
};
}  // namespace std
