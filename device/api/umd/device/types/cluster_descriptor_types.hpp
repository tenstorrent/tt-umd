/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

#include "umd/device/types/harvesting.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/semver.hpp"

// TODO: To be moved inside tt::umd namespace once all clients switch to namespace usage.
enum BoardType : uint32_t {
    E75,
    E150,
    E300,
    N150,
    N300,
    P100,
    P150,
    P300,
    GALAXY,
    UBB,
    QUASAR,
    UNKNOWN,
};

namespace tt::umd {

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

    constexpr bool operator<(const eth_coord_t &other) const noexcept {
        if (cluster_id != other.cluster_id) {
            return cluster_id < other.cluster_id;
        }
        if (x != other.x) {
            return x < other.x;
        }
        if (y != other.y) {
            return y < other.y;
        }
        if (rack != other.rack) {
            return rack < other.rack;
        }
        return shelf < other.shelf;
    }
};

// Centralized mapping from lowercase name (including aliases) to BoardType for fast lookup.
inline const std::unordered_map<std::string_view, BoardType> board_type_name_map = {
    // Canonical forms (stored in lowercase for case-insensitive lookup)
    {"e75", BoardType::E75},
    {"e150", BoardType::E150},
    {"e300", BoardType::E300},
    {"n150", BoardType::N150},
    {"n300", BoardType::N300},
    {"p100", BoardType::P100},
    {"p150", BoardType::P150},
    {"p300", BoardType::P300},
    {"galaxy", BoardType::GALAXY},
    {"ubb", BoardType::UBB},
    {"quasar", BoardType::QUASAR},
    {"unknown", BoardType::UNKNOWN},
    // Aliases (input only)
    {"p150a", BoardType::P150},
    {"p150c", BoardType::P150},
    {"p300a", BoardType::P300},
    {"p300c", BoardType::P300},
};

// Mapping from BoardType to canonical string name (keeps historical casing like "GALAXY").
inline const std::unordered_map<BoardType, std::string_view> board_type_canonical_name_map = {
    {BoardType::E75, "e75"},
    {BoardType::E150, "e150"},
    {BoardType::E300, "e300"},
    {BoardType::N150, "n150"},
    {BoardType::N300, "n300"},
    {BoardType::P100, "p100"},
    {BoardType::P150, "p150"},
    {BoardType::P300, "p300"},
    {BoardType::GALAXY, "galaxy"},
    {BoardType::UBB, "ubb"},
    {BoardType::QUASAR, "quasar"},
    {BoardType::UNKNOWN, "unknown"},
};

inline std::string board_type_to_string(const BoardType board_type) {
    if (auto it = board_type_canonical_name_map.find(board_type); it != board_type_canonical_name_map.end()) {
        return std::string(it->second);
    }
    throw std::runtime_error("Unknown board type passed for conversion to string.");
}

inline BoardType board_type_from_string(std::string_view board_type_str) {
    const std::string lowered = to_lower(std::string(board_type_str));

    if (auto it = board_type_name_map.find(lowered); it != board_type_name_map.end()) {
        return it->second;
    }

    return BoardType::UNKNOWN;
}

// We have two ways BH chips are connected to the rest of the system, either one of the two PCI cores can be active.
enum BlackholeChipType : uint32_t {
    Type1,
    Type2,
};

inline BlackholeChipType get_blackhole_chip_type(const BoardType board_type, const uint8_t asic_location) {
    if (asic_location != 0) {
        if (board_type != BoardType::P300) {
            throw std::runtime_error("Remote chip is supported only for Blackhole P300 board.");
        }
    }

    switch (board_type) {
        case BoardType::P100:
            return BlackholeChipType::Type1;
        case BoardType::P150:
            return BlackholeChipType::Type2;
        case BoardType::P300:
            switch (asic_location) {
                case 0:
                    return BlackholeChipType::Type2;
                case 1:
                    return BlackholeChipType::Type1;
                default:
                    throw std::runtime_error(
                        "Invalid asic location for Blackhole P300 board: " + std::to_string(asic_location));
            }
        default:
            throw std::runtime_error("Invalid board type for Blackhole architecture.");
    }
}

inline uint32_t get_number_of_chips_from_board_type(const BoardType board_type) {
    switch (board_type) {
        case BoardType::N150:
            return 1;
        case BoardType::N300:
            return 2;
        case BoardType::P100:
            return 1;
        case BoardType::P150:
            return 1;
        case BoardType::P300:
            return 2;
        case BoardType::GALAXY:
            return 1;
        case BoardType::UBB:
            return 32;
        default:
            throw std::runtime_error("Unknown board type for number of chips calculation.");
    }
}

inline const std::unordered_map<uint64_t, BoardType> board_upi_map = {
    // P100
    {0x36, BoardType::P100},
    {0x43, BoardType::P100},
    // P150
    {0x40, BoardType::P150},
    {0x41, BoardType::P150},
    {0x42, BoardType::P150},
    // P300
    {0x44, BoardType::P300},
    {0x45, BoardType::P300},
    {0x46, BoardType::P300},
    // N150
    {0x18, BoardType::N150},
    // N300
    {0x14, BoardType::N300},
    // GALAXY
    {0xB, BoardType::GALAXY},
    // UBB
    {0x35, BoardType::UBB},
};

inline BoardType get_board_type_from_board_id(const uint64_t board_id) {
    uint64_t upi = (board_id >> 36) & 0xFFFFF;

    auto board_type_it = board_upi_map.find(upi);
    if (board_type_it != board_upi_map.end())
        return board_type_it->second;

    throw std::runtime_error(fmt::format("No existing board type for board id 0x{:x}", board_id));
}

struct ChipUID {
    uint64_t board_id;
    uint8_t asic_location;

    bool operator<(const ChipUID &other) const {
        return std::tie(board_id, asic_location) < std::tie(other.board_id, other.asic_location);
    }

    bool const operator==(const ChipUID &other) const {
        return board_id == other.board_id && asic_location == other.asic_location;
    }
};

struct ChipInfo {
    bool noc_translation_enabled = false;
    HarvestingMasks harvesting_masks = {0, 0, 0, 0};
    BoardType board_type = BoardType::UNKNOWN;
    ChipUID chip_uid = {0, 0};
    uint8_t asic_location = 0;
};

enum class DramTrainingStatus : uint8_t {
    IN_PROGRESS = 0,
    FAIL = 1,
    SUCCESS = 2,
};

}  // namespace tt::umd

// TODO: To be removed once clients switch to namespace usage.
using tt::umd::chip_id_t;
using tt::umd::eth_coord_t;
using tt::umd::ethernet_channel_t;

namespace tt::umd {
using BoardType = ::BoardType;
}

namespace std {
template <>
struct hash<tt::umd::eth_coord_t> {
    std::size_t operator()(tt::umd::eth_coord_t const &c) const {
        std::size_t seed = 0;
        tt::umd::boost_hash_combine(seed, c.cluster_id);
        tt::umd::boost_hash_combine(seed, c.x);
        tt::umd::boost_hash_combine(seed, c.y);
        tt::umd::boost_hash_combine(seed, c.rack);
        tt::umd::boost_hash_combine(seed, c.shelf);
        return seed;
    }
};

}  // namespace std

namespace fmt {
template <>
struct formatter<eth_coord_t> {
    constexpr auto parse(fmt::format_parse_context &ctx) { return ctx.begin(); }

    template <typename Context>
    constexpr auto format(eth_coord_t const &coord, Context &ctx) const {
        return format_to(
            ctx.out(), "({}, {}, {}, {}, {})", coord.cluster_id, coord.x, coord.y, coord.rack, coord.shelf);
    }
};
}  // namespace fmt
