/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once 

#include <tuple>

using chip_id_t = int;
using ethernet_channel_t = int;
using eth_coord_t = std::tuple<int, int, int, int>;  // x, y, rack, shelf

namespace std {
template <>
struct hash<eth_coord_t> {
  std::size_t operator()(eth_coord_t const &c) const {
    std::size_t seed = 0;
    seed = std::hash<std::size_t>()(std::get<0>(c)) << 48 | 
          std::hash<std::size_t>()(std::get<1>(c)) << 32 |
          std::hash<std::size_t>()(std::get<2>(c)) << 16 |
          std::hash<std::size_t>()(std::get<3>(c));
    return seed;
  }
};
}
