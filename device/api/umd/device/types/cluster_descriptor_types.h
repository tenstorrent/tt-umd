/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>

#include <cstdint>
#include <functional>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <array>

#include "fmt/core.h"
#include "umd/device/semver.hpp"
#include "umd/device/types/harvesting.h"

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

// Centralized bidirectional mapping between BoardType and all accepted string representations.
// Canonical strings match historical serialization output (note: "GALAXY" is uppercase).
struct BoardTypeNameEntry {
    BoardType type;
    const char *name;
    bool is_canonical;
};

inline constexpr std::array<BoardTypeNameEntry, 15> board_type_name_entries = {{
    // Canonical forms
    {BoardType::E75, "e75", true},
    {BoardType::E150, "e150", true},
    {BoardType::E300, "e300", true},
    {BoardType::N150, "n150", true},
    {BoardType::N300, "n300", true},
    {BoardType::P100, "p100", true},
    {BoardType::P150, "p150", true},
    {BoardType::P300, "p300", true},
    {BoardType::GALAXY, "GALAXY", true},
    {BoardType::UBB, "ubb", true},
    {BoardType::UNKNOWN, "unknown", true},
    // Aliases (input only)
    {BoardType::P150, "p150A", false},
    {BoardType::P150, "p150C", false},
    {BoardType::P300, "p300A", false},
    {BoardType::P300, "p300C", false},
}};

inline std::string board_type_to_string(const BoardType board_type) {
    for (const auto &entry : board_type_name_entries) {
        if (entry.type == board_type && entry.is_canonical) {
            return entry.name;
        }
    }
    throw std::runtime_error("Unknown board type passed for conversion to string.");
}

inline std::optional<BoardType> board_type_from_string(std::string_view board_type_str) {
    auto to_lower = [](std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (char ch : s) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return out;
    };

    const std::string lowered = to_lower(board_type_str);

    for (const auto &entry : board_type_name_entries) {
        const std::string name_lower = to_lower(entry.name);
        if (lowered == name_lower) {
            return entry.type;
        }
    }

    return std::nullopt;
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

inline BoardType get_board_type_from_board_id(const uint64_t board_id) {
    uint64_t upi = (board_id >> 36) & 0xFFFFF;

    if (upi == 0x36) {
        return BoardType::P100;
    } else if (upi == 0x43) {
        return BoardType::P100;
    } else if (upi == 0x40 || upi == 0x41 || upi == 0x42) {
        return BoardType::P150;
    } else if (upi == 0x44 || upi == 0x45 || upi == 0x46) {
        return BoardType::P300;
    } else if (upi == 0x14) {
        return BoardType::N300;
    } else if (upi == 0x18) {
        return BoardType::N150;
    } else if (upi == 0xB) {
        return BoardType::GALAXY;
    } else if (upi == 0x35) {
        return BoardType::UBB;
    }

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
    HarvestingMasks harvesting_masks;
    BoardType board_type;
    ChipUID chip_uid;
    bool noc_translation_enabled;
    semver_t firmware_version = {0, 0, 0};
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
