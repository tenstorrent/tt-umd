// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/att_window.hpp"
#include "tt-umd/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "tt-umd/coordinates/att/configs/horizon_2x3_att_map.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
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

// three_window_map() with a full-tile window whose slots are 4 KiB instead of 256 bytes, so the
// full-tile window can carry Tensix offsets the L1 window cannot.
att::MapData tall_full_tile_map() {
    att::MapData map = three_window_map();
    att::Window& tile_window = map.windows[static_cast<size_t>(att::WindowClass::FULL_TILE)];
    tile_window.mask_bits = 16;
    tile_window.endpoint_shift = 12;
    return map;
}

TEST(AttResolver, ReachesTensixRegistersThroughTheFullTileWindow) {
    const att::EndpointResolver resolver(tall_full_tile_map());

    // L1 offsets keep resolving through the worker window.
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::TENSIX, 0xfc, 4), 0x100fcu);
    // The first offset past L1 is a register: it resolves through the full-tile window, whose
    // selector 0 names the same tile.
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::TENSIX, 0x100, 4), 0x40100u);
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::TENSIX, 0xffc, 4), 0x40ffcu);
}

TEST(AttResolver, RejectsATensixTransferThatRunsOutOfL1) {
    const att::EndpointResolver resolver(tall_full_tile_map());

    // A transfer that starts in L1 and crosses its end is not a register access even though the
    // full-tile window could hold it.
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::TENSIX, 0xfc, 8), std::runtime_error);
}

TEST(AttResolver, RejectsATensixOffsetPastTheFullTile) {
    const att::EndpointResolver resolver(tall_full_tile_map());

    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::TENSIX, 0x1000, 1), std::runtime_error);
    EXPECT_THROW(resolver.resolve({1, 1}, CoreType::TENSIX, 0xffc, 8), std::runtime_error);
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

namespace {

// The in-repo Quasar descriptor places its workers on the same grid as the qsr.s1 emulation model,
// so it exercises the descriptor-facing path over real coordinates.
SocDescriptor quasar_soc_descriptor() {
    return SocDescriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("quasar_32_arch.yaml")),
        {.noc_translation_enabled = false});
}

}  // namespace

TEST(AttResolveCore, ResolvesATypedCoreCoord) {
    const SocDescriptor soc_descriptor = quasar_soc_descriptor();
    const att::EndpointResolver resolver(att::GRENDEL_QSR1_MAP);
    const CoreCoord core(2, 2, CoreType::TENSIX, CoordSystem::NOC0);

    EXPECT_EQ(att::resolve_core(resolver, soc_descriptor, core, 0x1000, 4), 0x10000001000ULL);
}

TEST(AttResolveCore, ResolvesAnUntypedLiteralCoordinateLikeItsTypedForm) {
    const SocDescriptor soc_descriptor = quasar_soc_descriptor();
    const att::EndpointResolver resolver(att::GRENDEL_QSR1_MAP);

    // A client sends a coordinate it has already translated, carrying no core type. Both roles must
    // reach the same address or a client-mode access lands on a different core than a host one.
    const CoreCoord literal(2, 2, CoreType::UNSPECIFIED, CoordSystem::LITERAL);
    const CoreCoord typed(2, 2, CoreType::TENSIX, CoordSystem::NOC0);

    EXPECT_EQ(
        att::resolve_core(resolver, soc_descriptor, literal, 0x1000, 4),
        att::resolve_core(resolver, soc_descriptor, typed, 0x1000, 4));
}

// The Horizon 2x3 map sends a tile's selector in address bits 51:40, the encoding tt-metal's
// horizon_2x3 map gives kernels, so the host and the kernels address a tile identically.
TEST(AttHorizonMap, ResolvesEachTileToItsSelectorAtBit40) {
    const att::EndpointResolver resolver(att::HORIZON_2X3_MAP);
    // Tensix (0,0), selector 1: 0x0000'0100'0000'1000.
    EXPECT_EQ(resolver.resolve({0, 0}, CoreType::TENSIX, 0x1000, 4), (uint64_t{1} << 40) | 0x1000);
    // Tensix (1,0), selector 2: 0x0000'0200'0000'1000.
    EXPECT_EQ(resolver.resolve({1, 0}, CoreType::TENSIX, 0x1000, 4), (uint64_t{2} << 40) | 0x1000);
    // Dispatch (0,1), selector 3: 0x0000'0300'0000'0020.
    EXPECT_EQ(resolver.resolve({0, 1}, CoreType::DISPATCH, 0x20, 4), (uint64_t{3} << 40) | 0x20);
    // Dispatch (1,1), selector 4: 0x0000'0400'0000'0020.
    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::DISPATCH, 0x20, 4), (uint64_t{4} << 40) | 0x20);
    // NOC2AXI (0,2) as DRAM, selector 5: 0x0000'0500'016f'd880.
    EXPECT_EQ(resolver.resolve({0, 2}, CoreType::DRAM, 0x16fd880, 4), (uint64_t{5} << 40) | 0x16fd880);
    // NOC2AXI (1,2) as DRAM, selector 6: 0x0000'0600'016f'e880.
    EXPECT_EQ(resolver.resolve({1, 2}, CoreType::DRAM, 0x16fe880, 4), (uint64_t{6} << 40) | 0x16fe880);
    // The same NOC2AXI tiles' own registers (ROUTER_ONLY) resolve to the same selectors.
    // NOC2AXI (0,2), selector 5: 0x0000'0500'0000'0000.
    EXPECT_EQ(resolver.resolve({0, 2}, CoreType::ROUTER_ONLY, 0x0, 4), uint64_t{5} << 40);
    // NOC2AXI (1,2), selector 6: 0x0000'0600'0000'0000.
    EXPECT_EQ(resolver.resolve({1, 2}, CoreType::ROUTER_ONLY, 0x0, 4), uint64_t{6} << 40);
}

// Selector 0 is the issuing tile itself, so a bare address never names another core.
TEST(AttHorizonMap, LeavesSelectorZeroToTheTileItself) {
    for (const auto& words : att::HORIZON_2X3_MAP.endpoint_words) {
        ASSERT_FALSE(words.empty());
        EXPECT_EQ(words[0], att::ENDPOINT_UNPOPULATED);
    }
}

TEST(AttHorizonMap, RejectsATileOutsideTheIp) {
    const att::EndpointResolver resolver(att::HORIZON_2X3_MAP);
    EXPECT_THROW(resolver.resolve({2, 0}, CoreType::TENSIX, 0, 4), std::exception);
    EXPECT_THROW(resolver.resolve({0, 1}, CoreType::TENSIX, 0, 4), std::exception);
}

TEST(AttHorizonMap, CarriesAFortyBitLocalAddress) {
    const att::EndpointResolver resolver(att::HORIZON_2X3_MAP);
    EXPECT_NO_THROW(resolver.resolve({0, 2}, CoreType::DRAM, (uint64_t{1} << 40) - 4, 4));
    EXPECT_THROW(resolver.resolve({0, 2}, CoreType::DRAM, (uint64_t{1} << 40) - 2, 4), std::exception);
}
