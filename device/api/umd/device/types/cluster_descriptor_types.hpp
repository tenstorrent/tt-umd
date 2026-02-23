// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

#include "umd/device/utils/common.hpp"
#include "umd/device/utils/semver.hpp"

// Types in this file can be used without using the driver, hence they aren't in tt::umd namespace.
namespace tt {

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
    // There is both UBB and UBB_WORMHOLE board types in the system right now.
    // Since we want to deprecate UBB, we make UBB_WORMHOLE an alias to UBB.
    // Clients should remove UBB usage and switch to UBB_WORMHOLE.
    UBB,
    UBB_WORMHOLE = UBB,
    UBB_BLACKHOLE,
    QUASAR,
    UNKNOWN,
};

static_assert(E75 == 0, "E75 must be 0");
static_assert(E150 == 1, "E150 must be 1");
static_assert(E300 == 2, "E300 must be 2");
static_assert(N150 == 3, "N150 must be 3");
static_assert(N300 == 4, "N300 must be 4");
static_assert(P100 == 5, "P100 must be 5");
static_assert(P150 == 6, "P150 must be 6");
static_assert(P300 == 7, "P300 must be 7");
static_assert(GALAXY == 8, "GALAXY must be 8");
static_assert(UBB == 9, "UBB must be 9");
static_assert(UBB_WORMHOLE == 9, "WH_UBB must equal UBB");
static_assert(UBB_BLACKHOLE == 10, "BH_UBB must be 10");
static_assert(QUASAR == 11, "QUASAR must be 11");
static_assert(UNKNOWN == 12, "UNKNOWN must be 12");

// Small performant hash combiner taken from boost library.
// Not using boost::hash_combine due to dependency complications.
inline void boost_hash_combine(std::size_t &seed, const int value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

using ChipId = int;
using EthernetChannel = int;

struct EthCoord {
    int cluster_id;  // This is the same for connected chips.
    int x;
    int y;
    int rack;
    int shelf;

    // in C++20 this should be defined as:
    // constexpr bool operator==(const EthCoord &other) const noexcept = default;
    constexpr bool operator==(const EthCoord &other) const noexcept {
        return (
            cluster_id == other.cluster_id and x == other.x and y == other.y and rack == other.rack and
            shelf == other.shelf);
    }

    constexpr bool operator<(const EthCoord &other) const noexcept {
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
    // Canonical forms (stored in lowercase for case-insensitive lookup).
    {"e75", BoardType::E75},
    {"e150", BoardType::E150},
    {"e300", BoardType::E300},
    {"n150", BoardType::N150},
    {"n300", BoardType::N300},
    {"p100", BoardType::P100},
    {"p150", BoardType::P150},
    {"p300", BoardType::P300},
    {"ubb", BoardType::UBB},
    {"ubb_blackhole", BoardType::UBB_BLACKHOLE},
    {"ubb_wormhole", BoardType::UBB_WORMHOLE},
    {"quasar", BoardType::QUASAR},
    {"unknown", BoardType::UNKNOWN},
    // Aliases (input only).
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
    {BoardType::UBB, "ubb"},
    {BoardType::UBB_BLACKHOLE, "ubb_blackhole"},
    {BoardType::UBB_WORMHOLE, "ubb_wormhole"},
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
        case BoardType::P150:
            return 1;
        case BoardType::P300:
            return 2;
        // TODO: switch usage of UBB to UBB_WORMHOLE.
        case BoardType::UBB:
        case BoardType::UBB_BLACKHOLE:
            return 32;
        default:
            throw std::runtime_error("Unknown board type for number of chips calculation.");
    }
}

inline const std::unordered_map<uint64_t, BoardType> board_upi_map = {
    {0x36, BoardType::P100},
    {0x43, BoardType::P100},
    {0x40, BoardType::P150},
    {0x41, BoardType::P150},
    {0x42, BoardType::P150},
    {0x44, BoardType::P300},
    {0x45, BoardType::P300},
    {0x46, BoardType::P300},
    {0x18, BoardType::N150},
    {0x14, BoardType::N300},
    // TODO: move 0x35 constant to be equal to UBB_WORMHOLE once we delete UBB.
    {0x35, BoardType::UBB},
    {0x47, BoardType::UBB_BLACKHOLE}};

inline BoardType get_board_type_from_board_id(const uint64_t board_id) {
    uint64_t upi = (board_id >> 36) & 0xFFFFF;

    auto board_type_it = board_upi_map.find(upi);
    if (board_type_it != board_upi_map.end()) {
        return board_type_it->second;
    }

    throw std::runtime_error(fmt::format("No existing board type for board id 0x{:x}", board_id));
}

static const std::unordered_map<BoardType, uint32_t> expected_tensix_harvested_units_map = {
    {BoardType::N150, 1},
    {BoardType::N300, 2},
    {BoardType::P100, 2},
    {BoardType::P150, 2},
    {BoardType::P300, 2},
    {BoardType::UBB, 0},
    {BoardType::UBB_BLACKHOLE, 1},
};

static const std::unordered_map<BoardType, uint32_t> expected_dram_harvested_units_map = {
    {BoardType::N150, 0},
    {BoardType::N300, 0},
    {BoardType::P100, 1},
    {BoardType::P150, 0},
    {BoardType::P300, 0},
    {BoardType::UBB, 0},
    {BoardType::UBB_BLACKHOLE, 0},
};

static const std::unordered_map<BoardType, uint32_t> expected_eth_harvested_units_map = {
    {BoardType::N150, 0},
    {BoardType::N300, 0},
    {BoardType::P100, 14},
    {BoardType::P150, 2},
    {BoardType::P300, 2},
    {BoardType::UBB, 0},
    {BoardType::UBB_BLACKHOLE, 2},
};

struct HarvestingMasks {
    size_t tensix_harvesting_mask = 0;
    size_t dram_harvesting_mask = 0;
    size_t eth_harvesting_mask = 0;
    size_t pcie_harvesting_mask = 0;
    size_t l2cpu_harvesting_mask = 0;

    HarvestingMasks operator|(const HarvestingMasks &other) const {
        return HarvestingMasks{
            .tensix_harvesting_mask = this->tensix_harvesting_mask | other.tensix_harvesting_mask,
            .dram_harvesting_mask = this->dram_harvesting_mask | other.dram_harvesting_mask,
            .eth_harvesting_mask = this->eth_harvesting_mask | other.eth_harvesting_mask,
            .pcie_harvesting_mask = this->pcie_harvesting_mask | other.pcie_harvesting_mask,
            .l2cpu_harvesting_mask = this->l2cpu_harvesting_mask | other.l2cpu_harvesting_mask};
    }
};

struct ChipInfo {
    bool noc_translation_enabled = false;
    HarvestingMasks harvesting_masks = {0, 0, 0, 0};
    BoardType board_type = BoardType::UNKNOWN;
    uint64_t board_id = 0;
    uint8_t asic_location = 0;
};

enum class DramTrainingStatus : uint8_t {
    IN_PROGRESS = 0,
    FAIL = 1,
    SUCCESS = 2,
};
}  // namespace tt

namespace std {
template <>
struct hash<tt::EthCoord> {
    std::size_t operator()(tt::EthCoord const &c) const {
        std::size_t seed = 0;
        tt::boost_hash_combine(seed, c.cluster_id);
        tt::boost_hash_combine(seed, c.x);
        tt::boost_hash_combine(seed, c.y);
        tt::boost_hash_combine(seed, c.rack);
        tt::boost_hash_combine(seed, c.shelf);
        return seed;
    }
};

}  // namespace std

namespace fmt {
template <>
struct formatter<tt::EthCoord> {
    constexpr auto parse(fmt::format_parse_context &ctx) { return ctx.begin(); }

    template <typename Context>
    constexpr auto format(tt::EthCoord const &coord, Context &ctx) const {
        return format_to(
            ctx.out(), "({}, {}, {}, {}, {})", coord.cluster_id, coord.x, coord.y, coord.rack, coord.shelf);
    }
};
}  // namespace fmt
