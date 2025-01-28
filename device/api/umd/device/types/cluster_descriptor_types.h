/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>

#include <cstdint>
#include <functional>

#include "umd/device/types/harvesting.h"

// Small performant hash combiner taken from boost library.
// Not using boost::hash_combine due to dependency complications.
inline void boost_hash_combine(std::size_t &seed, const int value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

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

enum BoardType : uint32_t {
    E75,
    E150,
    E300,
    N150,
    N300,
    P100,
    P150A,
    P300,
    GALAXY,
    UNKNOWN,
};

// TODO: add Wormhole and Grayskull board types to this function
inline BoardType get_board_type_from_board_id(const uint64_t board_id) {
    uint64_t upi = (board_id >> 36) & 0xFFFFF;

    if (upi == 0x36) {
        return BoardType::P100;
    } else if (upi == 0x43) {
        return BoardType::P100;
    } else if (upi == 0x40 || upi == 0x41) {
        return BoardType::P150A;
    }

    throw std::runtime_error(fmt::format("No existing board type for board id {}", board_id));
}

struct ChipUID {
    uint64_t board_id;
    uint8_t asic_location;

    bool operator<(const ChipUID &other) const {
        return std::tie(board_id, asic_location) < std::tie(other.board_id, other.asic_location);
    }

    bool const operator==(const ChipUID &other) const {
        return board_id == other.board_id and asic_location == other.asic_location;
    }
};

struct ChipInfo {
    tt::umd::HarvestingMasks harvesting_masks;
    BoardType board_type;
    ChipUID chip_uid;
    bool noc_translation_enabled;
};

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
