/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>

#include <functional>
#include <type_traits>


namespace tt::umd {

enum class chip_id : int {
    none = -1,
};

enum class ethernet_channel : int {};

struct eth_coord {
    int x;
    int y;
    int rack;
    int shelf;

    // in C++20 this should be defined as:
    // constexpr bool operator==(const eth_coord &other) const noexcept = default;
    constexpr bool operator==(const eth_coord &other) const noexcept {
        return (x == other.x and y == other.y and rack == other.rack and shelf == other.shelf);
    }
};

}  // namespace tt::umd

using chip_id_t = tt::umd::chip_id;
using ethernet_channel_t = tt::umd::ethernet_channel;
using eth_coord_t = tt::umd::eth_coord;

template <>
struct fmt::formatter<tt::umd::chip_id> {
   private:
    using underlying_type = std::underlying_type_t<tt::umd::chip_id>;

    fmt::formatter<underlying_type> underlying_formatter;

   public:
    constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx) {
        return underlying_formatter.parse(ctx);
    }

    constexpr fmt::format_context::iterator format(const tt::umd::chip_id value, fmt::format_context &ctx) const {
        return underlying_formatter.format(static_cast<underlying_type>(value), ctx);
    } 
};

template <>
struct std::hash<tt::umd::eth_coord> {
    std::size_t operator()(tt::umd::eth_coord const &c) const {
        constexpr std::hash<std::size_t> hash{};
        const std::size_t seed = hash(c.x) << 48 | hash(c.y) << 32 | c.rack << 16 | c.shelf;
        return seed;
    }
};
