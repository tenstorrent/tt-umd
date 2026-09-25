// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/att_window.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// Three windows at distinct bases, each with a 4-bit selector over a 256-byte slot, so a resolved
// address says plainly which window produced it and a transfer can be walked off the end of a slot.
constexpr att::Window WORKER_WINDOW{
    .compare = 0x10000,
    .mask_bits = 12,
    .endpoint_shift = 8,
    .endpoint_size = 4,
    .endpoint_table_offset = 0,
    .translate_address = true,
};

constexpr att::Window DRAM_WINDOW{
    .compare = 0x20000,
    .mask_bits = 12,
    .endpoint_shift = 8,
    .endpoint_size = 4,
    .endpoint_table_offset = 16,
    .translate_address = false,
};

constexpr att::Window FULL_TILE_WINDOW{
    .compare = 0x40000,
    .mask_bits = 12,
    .endpoint_shift = 8,
    .endpoint_size = 4,
    .endpoint_table_offset = 32,
    .translate_address = true,
};

// Package coordinates, packed the way an endpoint word is. The worker and full-tile tables name the
// same tile at selector 0, which is what lets a test show the core type choosing the window.
constexpr uint16_t WORKER_WORDS[] = {0x082, 0x083};     // package (2,2) and (3,2)
constexpr uint16_t DRAM_WORDS[] = {0x145};              // package (5,5)
constexpr uint16_t FULL_TILE_WORDS[] = {0x082, 0x249};  // package (2,2) and (9,9)

constexpr uint32_t PACKAGE_OFFSET = 1;

// A map with every role populated, so resolution can be exercised through each window.
att::MapData three_window_map() {
    att::MapData map{};
    map.windows[static_cast<size_t>(att::WindowClass::WORKER)] = WORKER_WINDOW;
    map.windows[static_cast<size_t>(att::WindowClass::DRAM)] = DRAM_WINDOW;
    map.windows[static_cast<size_t>(att::WindowClass::FULL_TILE)] = FULL_TILE_WINDOW;
    map.endpoint_words[static_cast<size_t>(att::WindowClass::WORKER)] = WORKER_WORDS;
    map.endpoint_words[static_cast<size_t>(att::WindowClass::DRAM)] = DRAM_WORDS;
    map.endpoint_words[static_cast<size_t>(att::WindowClass::FULL_TILE)] = FULL_TILE_WORDS;
    map.package_offset_x = PACKAGE_OFFSET;
    map.package_offset_y = PACKAGE_OFFSET;
    return map;
}

// A map with one populated window, for exercising the checks the resolver makes on its input.
att::MapData map_with_worker_endpoints(const att::Window& window, att::Table<uint16_t> words) {
    att::MapData map{};
    map.windows[static_cast<size_t>(att::WindowClass::WORKER)] = window;
    map.endpoint_words[static_cast<size_t>(att::WindowClass::WORKER)] = words;
    return map;
}

// Two selector values, so a third endpoint row has no selector that can reach it.
constexpr att::Window TWO_SELECTOR_WINDOW{
    .compare = 0x1000,
    .mask_bits = 8,
    .endpoint_shift = 7,
    .endpoint_size = 1,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

}  // namespace

TEST(AttResolver, RejectsAnEndpointTableLongerThanItsSelectorField) {
    constexpr uint16_t words[] = {0x104, 0x105, 0x106};

    EXPECT_THROW(att::EndpointResolver{map_with_worker_endpoints(TWO_SELECTOR_WINDOW, words)}, std::runtime_error);
}

TEST(AttResolver, RejectsAnEndpointTableThatNamesOneCoreTwice) {
    // A repeated coordinate leaves the core reachable through either slot, so a coordinate lookup
    // over the table cannot say which one the caller meant.
    constexpr uint16_t words[] = {0x104, 0x104};

    EXPECT_THROW(att::EndpointResolver{map_with_worker_endpoints(TWO_SELECTOR_WINDOW, words)}, std::runtime_error);
}

TEST(AttResolver, ResolvesEachRoleThroughItsOwnWindow) {
    const att::EndpointResolver resolver(three_window_map());

    // Descriptor (1,1) is package (2,2), selector 0 of both the worker and the full-tile table.
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::TENSIX, 0x10, 4), 0x10010u);
    EXPECT_EQ(resolver.resolve({4, 4}, CoreType::DRAM, 0x10, 4), 0x20010u);
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::ROUTER_ONLY, 0x10, 4), 0x40010u);
}

TEST(AttResolver, TakesTheSelectorFromTheTableRatherThanTheCoordinate) {
    const att::EndpointResolver resolver(three_window_map());

    // The full-tile table names package (9,9) at selector 1. Nothing about the coordinate says 1;
    // only its row in the table does.
    EXPECT_EQ(resolver.resolve({8, 8}, CoreType::ROUTER_ONLY, 0, 4), 0x40100u);
    EXPECT_EQ(resolver.resolve({2, 1}, CoreType::TENSIX, 0, 4), 0x10100u);
}

TEST(AttResolver, AppliesThePackageOffsetToTheLookup) {
    // The tables are written in the package frame, so the same descriptor coordinate resolves to a
    // different row once the offset changes, and the unoffset coordinate is not in the map at all.
    att::MapData shifted = three_window_map();
    shifted.package_offset_x = 0;
    shifted.package_offset_y = 0;
    const att::EndpointResolver resolver(shifted);

    EXPECT_EQ(resolver.resolve({2, 2}, CoreType::TENSIX, 0, 4), 0x10000u);
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::TENSIX, 0, 4), std::runtime_error);
}

TEST(AttResolver, RejectsACoreTheMapDoesNotName) {
    const att::EndpointResolver resolver(three_window_map());

    // In no table, so it is refused rather than aliased onto a tile that shares no address bits.
    EXPECT_THROW(resolver.resolve({7, 3}, CoreType::TENSIX, 0, 4), std::runtime_error);

    // In the worker table, but asked for through a window whose table does not hold it.
    EXPECT_THROW(resolver.resolve({2, 1}, CoreType::DRAM, 0, 4), std::runtime_error);
}

TEST(AttResolver, RejectsATransferThatRunsPastTheSlot) {
    const att::EndpointResolver resolver(three_window_map());

    // The slot is 256 bytes, so the last four fit and anything crossing the end does not: it would
    // silently land on the next selector's core.
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::TENSIX, 0xfc, 4), 0x100fcu);
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::TENSIX, 0xfc, 8), std::runtime_error);
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::TENSIX, 0x100, 1), std::runtime_error);
}

TEST(AttResolver, RejectsACoordinateThatWouldNotFitAnEndpointWord) {
    const att::EndpointResolver resolver(three_window_map());

    // An axis is six bits in an endpoint word, and packing ORs the two axes together, so an x past
    // the frame spills into y. Descriptor (129,1) is package (130,2), which packs to 0x082 -- the
    // very word the table holds for package (2,2). Unchecked it resolves to that core's selector
    // and the access lands on a real but wrong tile.
    EXPECT_THROW(resolver.resolve({129, 1}, CoreType::TENSIX, 0, 4), std::runtime_error);

    // Out of frame without colliding with a populated row is refused on the same grounds.
    EXPECT_THROW(resolver.resolve({63, 1}, CoreType::TENSIX, 0, 4), std::runtime_error);
}

TEST(AttResolver, RejectsACoreTypeNoWindowServes) {
    const att::EndpointResolver resolver(three_window_map());

    // A placeholder type selects no aperture. Mapping it onto the config window would send the
    // access to whichever core that table happens to hold at the coordinate.
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::HARVESTED, 0, 4), std::runtime_error);
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::UNSPECIFIED, 0, 4), std::runtime_error);
}
