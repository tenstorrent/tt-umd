// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/coordinates/att/att_resolver.hpp"
#include "umd/device/coordinates/att/att_window.hpp"
#include "umd/device/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

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
