/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once 

#include <boost/container_hash/hash.hpp>

#include <functional>
#include <tuple>

using chip_id_t = int;
using ethernet_channel_t = int;
struct eth_coord_t {
    int cluster_id; // This is the same for connected chips.
    int x;
    int y;
    int rack;
    int shelf;

    // in C++20 this should be defined as:
    // constexpr bool operator==(const eth_coord_t &other) const noexcept = default;
    constexpr bool operator==(const eth_coord_t &other) const noexcept {
        return (x == other.x and y == other.y and rack == other.rack and shelf == other.shelf);
    }
};

namespace std {
template <>
struct hash<eth_coord_t> {
  std::size_t operator()(eth_coord_t const &c) const {
    std::size_t seed = 0;
    boost::hash_combine(seed, c.cluster_id);
    boost::hash_combine(seed, c.x);
    boost::hash_combine(seed, c.y);
    boost::hash_combine(seed, c.rack);
    boost::hash_combine(seed, c.shelf);
    return seed;
  }
};
}
