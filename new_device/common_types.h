/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <tuple>
#include <filesystem>

#include "xy_pair.h"

namespace tt::umd {

enum class Arch {
    JAWBRIDGE = 0,
    GRAYSKULL,
    WORMHOLE,
    WORMHOLE_B0,
    BLACKHOLE,
    Invalid = 0xFF,
};

const std::filesystem::path repo_root = std::filesystem::path(__FILE__).parent_path();

using chip_id_t = int;
using ethernet_channel_t = int;
// brosko: turn this to struct
using eth_coord_t = std::tuple<int, int, int, int>;  // x, y, rack, shelf

} // namespace tt::umd


namespace std {
template <>
struct hash<tt::umd::eth_coord_t> {
    std::size_t operator()(tt::umd::eth_coord_t const &c) const {
        std::size_t seed = 0;
        seed = std::hash<std::size_t>()(std::get<0>(c)) << 48 | std::hash<std::size_t>()(std::get<1>(c)) << 32 |
               std::hash<std::size_t>()(std::get<2>(c)) << 16 | std::hash<std::size_t>()(std::get<3>(c));
        return seed;
    }
};

}  // namespace std
