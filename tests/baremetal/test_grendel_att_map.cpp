// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/coordinates/att/att_resolver.hpp"
#include "umd/device/coordinates/att/att_window.hpp"
#include "umd/device/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

// A real window shape to exercise the address math against, kept independent of the product maps
// so that changing one does not move these expectations.
constexpr att::Window NEO_L1_WINDOW{
    .compare = 0x10000000000ULL,
    .mask_bits = 30,
    .endpoint_shift = 24,
    .endpoint_size = 6,
    .endpoint_table_offset = 128,
    .translate_address = true,
};

TEST(GrendelAttWindow, MakeAddressComposesBaseSelectorAndOffset) {
    // Selector 0 at offset 0 is the window base itself.
    EXPECT_EQ(NEO_L1_WINDOW.make_address(0, 0), 0x10000000000ULL);

    // The selector occupies bits [29:24], so each step is one 16 MiB tile.
    EXPECT_EQ(NEO_L1_WINDOW.make_address(1, 0), 0x10001000000ULL);
    EXPECT_EQ(NEO_L1_WINDOW.make_address(31, 0), 0x1001f000000ULL);

    // The local offset passes through the low bits untouched.
    EXPECT_EQ(NEO_L1_WINDOW.make_address(0, 0x100000), 0x10000100000ULL);
}

TEST(GrendelAttWindow, AddressDecodesBackToSelectorAndOffset) {
    const uint64_t address = NEO_L1_WINDOW.make_address(17, 0x2000);

    EXPECT_EQ(NEO_L1_WINDOW.selector(address), 17u);
    EXPECT_EQ(NEO_L1_WINDOW.endpoint_index(address), 128 + 17);
    EXPECT_EQ(NEO_L1_WINDOW.local_address(address), 0x2000ULL);
}

TEST(GrendelAttWindow, RejectsTransfersThatSpillIntoTheNextSlot) {
    // The NEO L1 window addresses one tile per 1 << 24 bytes.
    EXPECT_TRUE(NEO_L1_WINDOW.transfer_supported(0x1000, 4));
    EXPECT_TRUE(NEO_L1_WINDOW.transfer_supported(0xfffffc, 4));

    // Spilling past the granule silently lands on the next tile, so it is refused.
    EXPECT_FALSE(NEO_L1_WINDOW.transfer_supported(0xfffffc, 8));
    EXPECT_FALSE(NEO_L1_WINDOW.transfer_supported(0x1000000, 1));
    EXPECT_FALSE(NEO_L1_WINDOW.transfer_supported(0x1000, 0));
}

TEST(GrendelAttWindow, RejectsSelectorsWiderThanTheField) {
    EXPECT_TRUE(NEO_L1_WINDOW.selector_supported(63));
    EXPECT_FALSE(NEO_L1_WINDOW.selector_supported(64));
}

// Coordinates below are in the frame of the qsr.s1 soc descriptor
// (tt-umd-simulators emu/qsr-s1-t6x1_DM/qsr-s1-t6x1_DM_emu.yaml), not the POR package frame.

TEST(GrendelAttMap, ResolvesTheFunctionalWorkerThroughTheNeoL1Window) {
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    // t6x1 populates a single NEO tile, the first slot of the L1 window.
    EXPECT_EQ(resolver.resolve({2, 2}, CoreType::TENSIX, 0x100000, 4), 0x10000100000ULL);
}

TEST(GrendelAttMap, ResolvesTheSmuThroughTheConfigWindow) {
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    EXPECT_EQ(resolver.resolve({1, 1}, CoreType::ROUTER_ONLY, 0, 4), 0x19d8000000ULL);
}

TEST(GrendelAttMap, ResolvesPerimeterCoresInEndpointTableOrder) {
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    // The east column occupies descending slots, so a slot derived from the coordinate rather than
    // from the endpoint table would place these two the other way round.
    EXPECT_EQ(resolver.resolve({10, 2}, CoreType::ROUTER_ONLY, 0, 4), 0x1978000000ULL);
    EXPECT_EQ(resolver.resolve({10, 5}, CoreType::ROUTER_ONLY, 0, 4), 0x1960000000ULL);
}

TEST(GrendelAttMap, ResolvesBothD2dLinksOfADramChannelToDistinctSlots) {
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    // A Mimir fronts its GDDR over two D2D links, and the descriptor lists a core for each. The
    // links are 16 selectors apart, so resolving a DRAM core by its channel alone would collapse
    // both onto the first link.
    EXPECT_EQ(resolver.resolve({4, 7}, CoreType::DRAM, 0, 4), 0x1000000000000ULL);
    EXPECT_EQ(resolver.resolve({5, 7}, CoreType::DRAM, 0, 4), 0x1002000000000ULL);

    // The next channel is one selector along from the first.
    EXPECT_EQ(resolver.resolve({8, 7}, CoreType::DRAM, 0, 4), 0x1000200000000ULL);
}

namespace {

// A map with one populated window, for exercising the checks the resolver makes on its input.
att::MapData map_with_worker_endpoints(const att::Window& window, att::Table<uint16_t> words) {
    att::MapData map{};
    map.windows[static_cast<size_t>(att::WindowClass::Worker)] = window;
    map.endpoint_words[static_cast<size_t>(att::WindowClass::Worker)] = words;
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

TEST(GrendelAttMap, RejectsAnEndpointTableLongerThanItsSelectorField) {
    constexpr uint16_t words[] = {0x104, 0x105, 0x106};

    EXPECT_THROW(att::Resolver{map_with_worker_endpoints(TWO_SELECTOR_WINDOW, words)}, std::runtime_error);
}

TEST(GrendelAttMap, RejectsAnEndpointTableThatNamesOneCoreTwice) {
    // A repeated coordinate leaves the core reachable through either slot, so a coordinate lookup
    // over the table cannot say which one the caller meant.
    constexpr uint16_t words[] = {0x104, 0x104};

    EXPECT_THROW(att::Resolver{map_with_worker_endpoints(TWO_SELECTOR_WINDOW, words)}, std::runtime_error);
}

TEST(GrendelAttMap, RejectsACoreTheMapDoesNotReach) {
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    // Keraunos sits outside the Quasar mesh and is reached through a window this map does not
    // carry, so it is refused rather than aliased onto a mesh tile that shares no address bits.
    EXPECT_THROW(resolver.resolve({11, 2}, CoreType::ROUTER_ONLY, 0, 4), std::runtime_error);
}

TEST(GrendelAttMap, EveryEndpointRowResolvesBackToItsOwnSlot) {
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);
    const CoreType core_types[] = {CoreType::TENSIX, CoreType::DRAM, CoreType::ROUTER_ONLY};

    for (size_t window_class = 0; window_class < att::WINDOW_CLASS_COUNT; ++window_class) {
        const att::Window& window = att::GRENDEL_QSR1_MAP.windows[window_class];
        const att::Table<uint16_t>& words = att::GRENDEL_QSR1_MAP.endpoint_words[window_class];

        for (uint32_t selector = 0; selector < words.size(); ++selector) {
            if (words[selector] == att::ENDPOINT_UNPOPULATED) {
                continue;
            }
            const tt_xy_pair core(
                (words[selector] & 0x3f) - att::GRENDEL_QSR1_MAP.package_offset_x,
                (words[selector] >> 6) - att::GRENDEL_QSR1_MAP.package_offset_y);

            const uint64_t address = resolver.resolve(core, core_types[window_class], 0, 4);

            EXPECT_EQ(window.selector(address), selector);
            EXPECT_EQ(window.endpoint_index(address), window.endpoint_table_offset + selector);
        }
    }
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

TEST(GrendelAttResolveCore, ResolvesATypedCoreCoord) {
    const SocDescriptor soc_descriptor = quasar_soc_descriptor();
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);
    const CoreCoord core(2, 2, CoreType::TENSIX, CoordSystem::NOC0);

    EXPECT_EQ(att::resolve_core(resolver, soc_descriptor, core, 0x1000, 4), 0x10000001000ULL);
}

TEST(GrendelAttResolveCore, ResolvesAnUntypedLiteralCoordinateLikeItsTypedForm) {
    const SocDescriptor soc_descriptor = quasar_soc_descriptor();
    const att::Resolver resolver(att::GRENDEL_QSR1_MAP);

    // A client sends a coordinate it has already translated, carrying no core type. Both roles must
    // reach the same address or a client-mode access lands on a different core than a host one.
    const CoreCoord literal(2, 2, CoreType::UNSPECIFIED, CoordSystem::LITERAL);
    const CoreCoord typed(2, 2, CoreType::TENSIX, CoordSystem::NOC0);

    EXPECT_EQ(
        att::resolve_core(resolver, soc_descriptor, literal, 0x1000, 4),
        att::resolve_core(resolver, soc_descriptor, typed, 0x1000, 4));
}

TEST(GrendelAttWindow, MatchesOnlyAddressesInsideTheWindow) {
    EXPECT_TRUE(NEO_L1_WINDOW.matches(NEO_L1_WINDOW.make_address(0, 0)));
    EXPECT_TRUE(NEO_L1_WINDOW.matches(NEO_L1_WINDOW.make_address(63, 0xffffff)));

    // The per-tile config window sits below this one and must not be claimed by it.
    EXPECT_FALSE(NEO_L1_WINDOW.matches(0x1800000000ULL));
    EXPECT_FALSE(NEO_L1_WINDOW.matches(0x1000000000000ULL));
}

// The map is a transcription, so it is checked against the tables it was transcribed from rather
// than against itself. tests/att_maps/grendel_qsr1_att_tables.yaml holds those tables; refreshing it
// from grendelemulation is what turns a POR move into a failure here instead of a misrouted access.
TEST(GrendelAttMap, MatchesTheTablesTheFirmwareProgrammes) {
    const YAML::Node tables = YAML::LoadFile(test_utils::GetAbsPath("att_maps/grendel_qsr1_att_tables.yaml"));

    for (const auto& entry : tables["mask_table"]) {
        const uint64_t compare = entry["compare"].as<uint64_t>();
        const att::Window* window = nullptr;
        for (const att::Window& candidate : att::GRENDEL_QSR1_MAP.windows) {
            if (candidate.compare == compare) {
                window = &candidate;
            }
        }
        ASSERT_NE(window, nullptr) << "no window for mask entry " << entry["index"].as<int>();

        SCOPED_TRACE("mask entry " + std::to_string(entry["index"].as<int>()));
        EXPECT_EQ(window->mask_bits, entry["mask"].as<uint32_t>());
        EXPECT_EQ(window->endpoint_shift, entry["ep_idx"].as<uint32_t>());
        EXPECT_EQ(window->endpoint_size, entry["ep_id_size"].as<uint32_t>());
        EXPECT_EQ(window->endpoint_table_offset, entry["table_offset"].as<uint32_t>());
        EXPECT_EQ(window->translate_address, entry["translate_addr"].as<int>() == 1);
    }

    // Every programmed row must be reachable through the window whose table holds it, and every row
    // the map declares populated must be one the firmware actually programmes.
    std::map<int, std::pair<uint32_t, uint32_t>> programmed;
    for (const auto& row : tables["noc_endpoint_table"]) {
        programmed.emplace(row["index"].as<int>(), std::make_pair(row["x"].as<uint32_t>(), row["y"].as<uint32_t>()));
    }

    size_t matched_rows = 0;
    for (size_t window_class = 0; window_class < att::WINDOW_CLASS_COUNT; ++window_class) {
        const att::Window& window = att::GRENDEL_QSR1_MAP.windows[window_class];
        const att::Table<uint16_t>& words = att::GRENDEL_QSR1_MAP.endpoint_words[window_class];

        for (uint32_t selector = 0; selector < words.size(); ++selector) {
            const int index = window.endpoint_table_offset + static_cast<int>(selector);
            const auto row = programmed.find(index);

            SCOPED_TRACE("endpoint row " + std::to_string(index));
            if (words[selector] == att::ENDPOINT_UNPOPULATED) {
                EXPECT_EQ(row, programmed.end()) << "row is programmed but the map leaves it unpopulated";
                continue;
            }
            ASSERT_NE(row, programmed.end()) << "map declares a row the firmware does not programme";
            EXPECT_EQ(words[selector] & 0x3f, row->second.first);
            EXPECT_EQ(words[selector] >> 6, row->second.second);
            ++matched_rows;
        }
    }
    EXPECT_EQ(matched_rows, programmed.size());
}
