// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <tuple>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/coordinates/att/att_map.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/att_window.hpp"
#include "tt-umd/coordinates/att/configs/horizon_x2_att_map.hpp"
#include "tt-umd/coordinates/att/configs/horizon_x2_columns_att_map.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// The horizon_x2 tables by access point, as horizon_x2's ip_layout.yaml orders them, and the device
// each one reaches.
struct AccessPointMap {
    const att::MapData* map;
    int device;
};

const AccessPointMap ACCESS_POINT_MAPS[] = {
    {&att::HORIZON_X2_DEV0_NOC2AXI0_MAP, 0},
    {&att::HORIZON_X2_DEV0_NOC2AXI1_MAP, 0},
    {&att::HORIZON_X2_DEV1_NOC2AXI0_MAP, 1},
    {&att::HORIZON_X2_DEV1_NOC2AXI1_MAP, 1},
};

// Device @p device's tables in the snapshot the testbench programs.
YAML::Node device_tables(int device) {
    const YAML::Node tables = YAML::LoadFile(test_utils::GetAbsPath("att_maps/horizon_x2_per_noc2axi.yaml"));
    for (const auto& entry : tables["devices"]) {
        if (entry["id"].as<int>() == device) {
            return entry["defaults"];
        }
    }
    ADD_FAILURE() << "the snapshot has no device " << device;
    return {};
}

// The horizon_x2_columns maps, one per column device, and the device and column of the NOC2AXI
// whose tables each one transcribes.
struct ColumnMap {
    const att::MapData* map;
    int device;
    int column;
};

const ColumnMap COLUMN_MAPS[] = {
    {&att::HORIZON_X2_DEV0_COLUMN0_MAP, 0, 0},
    {&att::HORIZON_X2_DEV0_COLUMN1_MAP, 0, 1},
    {&att::HORIZON_X2_DEV1_COLUMN0_MAP, 1, 0},
    {&att::HORIZON_X2_DEV1_COLUMN1_MAP, 1, 1},
};

// The tables of NOC2AXI (@p column, 2) of device @p device in the column snapshot.
YAML::Node column_noc2axi_tables(int device, int column) {
    const YAML::Node tables = YAML::LoadFile(test_utils::GetAbsPath("att_maps/horizon_x2_columns.yaml"));
    for (const auto& entry : tables["devices"]) {
        if (entry["id"].as<int>() != device) {
            continue;
        }
        for (const auto& tile : entry["tiles"]) {
            if (tile["tile"][0].as<int>() == column && tile["tile"][1].as<int>() == 2) {
                return tile;
            }
        }
    }
    ADD_FAILURE() << "the column snapshot has no NOC2AXI (" << column << ", 2) on device " << device;
    return {};
}

// The mask entry whose window starts at @p compare.
YAML::Node mask_entry_at(const YAML::Node& tables, uint64_t compare) {
    for (const auto& entry : tables["mask_table"]) {
        if (std::stoull(entry["compare"].as<std::string>(), nullptr, 16) == compare) {
            return entry;
        }
    }
    return {};
}

// Endpoint table row @p row, packed as the map packs it: (y << 6) | x.
uint16_t row_word(const YAML::Node& tables, uint32_t row) {
    for (const auto& entry : tables["noc_endpoint_table"]) {
        if (entry["index"].as<uint32_t>() == row) {
            return static_cast<uint16_t>((entry["y"].as<uint32_t>() << 6) | entry["x"].as<uint32_t>());
        }
    }
    ADD_FAILURE() << "the snapshot has no endpoint row " << row;
    return att::ENDPOINT_UNPOPULATED;
}

// Expects @p map's @p window_class window and its endpoint words to be the mask entry at the same
// compare in @p tables and the rows it resolves through.
void expect_window_matches(const att::MapData& map, att::WindowClass window_class, const YAML::Node& tables) {
    const auto index = static_cast<size_t>(window_class);
    const att::Window& window = map.windows[index];

    const YAML::Node mask_entry = mask_entry_at(tables, window.compare);
    ASSERT_TRUE(mask_entry.IsDefined()) << "no mask entry at 0x" << std::hex << window.compare;
    EXPECT_EQ(window.mask_bits, mask_entry["mask"].as<uint32_t>());
    EXPECT_EQ(window.endpoint_shift, mask_entry["ep_idx"].as<uint32_t>());
    EXPECT_EQ(window.endpoint_size, mask_entry["ep_id_size"].as<uint32_t>());
    EXPECT_EQ(window.endpoint_table_offset, mask_entry["table_offset"].as<uint32_t>());
    EXPECT_EQ(window.translate_address, mask_entry["translate_addr"].as<int>() == 1);

    const att::Table<uint16_t>& words = map.endpoint_words[index];
    for (uint32_t selector = 0; selector < words.size(); selector++) {
        EXPECT_EQ(words[selector], row_word(tables, window.endpoint_table_offset + selector))
            << "selector " << selector;
    }
}

}  // namespace

// The maps are transcriptions, so they are checked against the tables they were transcribed from:
// tests/att_maps/horizon_x2_per_noc2axi.yaml.
class HorizonX2RebasedWindow : public ::testing::TestWithParam<std::tuple<size_t, att::WindowClass>> {};

TEST_P(HorizonX2RebasedWindow, MatchesTheTables) {
    const auto [access_point, window_class] = GetParam();
    const AccessPointMap& entry = ACCESS_POINT_MAPS[access_point];
    expect_window_matches(*entry.map, window_class, device_tables(entry.device));
}

INSTANTIATE_TEST_SUITE_P(
    L1AndCsr,
    HorizonX2RebasedWindow,
    ::testing::Combine(
        ::testing::Values(0, 1, 2, 3), ::testing::Values(att::WindowClass::WORKER, att::WindowClass::FULL_TILE)));

// The tables hold each DRAM bank as its own pass-through window; the maps fold the two into one
// window with a one-bit selector, which must give the same addresses and reach the same NOC2AXI tiles.
class HorizonX2DramBank : public ::testing::TestWithParam<std::tuple<size_t, uint32_t>> {};

TEST_P(HorizonX2DramBank, MatchesTheTables) {
    const auto [access_point, bank] = GetParam();
    const AccessPointMap& entry = ACCESS_POINT_MAPS[access_point];
    const YAML::Node tables = device_tables(entry.device);
    const auto index = static_cast<size_t>(att::WindowClass::DRAM);
    const att::Window& window = entry.map->windows[index];

    const YAML::Node mask_entry = mask_entry_at(tables, window.make_address(bank, 0));
    ASSERT_TRUE(mask_entry.IsDefined());
    EXPECT_EQ(mask_entry["mask"].as<uint32_t>(), window.endpoint_shift);
    EXPECT_EQ(mask_entry["ep_id_size"].as<uint32_t>(), 0u);
    // The folded window spans both 1 GB banks.
    EXPECT_EQ(window.mask_bits, 31u);
    EXPECT_EQ(window.translate_address, mask_entry["translate_addr"].as<int>() == 1);
    EXPECT_EQ(entry.map->endpoint_words[index][bank], row_word(tables, mask_entry["table_offset"].as<uint32_t>()));
}

INSTANTIATE_TEST_SUITE_P(
    BothBanks, HorizonX2DramBank, ::testing::Combine(::testing::Values(0, 1, 2, 3), ::testing::Values(0u, 1u)));

// Each access point reaches the whole device in its own range: the addresses aether's AttConfig
// resolver gives for horizon_x2_per_noc2axi.yaml through that access point's NOC2AXI.
struct ResolveCase {
    size_t access_point;
    tt_xy_pair core;
    CoreType type;
    uint64_t offset;
    uint64_t address;
};

class HorizonX2Resolve : public ::testing::TestWithParam<ResolveCase> {};

TEST_P(HorizonX2Resolve, GivesTheTestbenchAddress) {
    const ResolveCase& c = GetParam();
    const att::EndpointResolver resolver(*ACCESS_POINT_MAPS[c.access_point].map);
    EXPECT_EQ(resolver.resolve(c.core, c.type, c.offset, 4), c.address);
}

INSTANTIATE_TEST_SUITE_P(
    PerAccessPoint,
    HorizonX2Resolve,
    ::testing::Values(
        ResolveCase{0, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x10'4085'0000},
        ResolveCase{1, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x11'4085'0000},
        ResolveCase{2, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x20'4085'0000},
        ResolveCase{3, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x21'4085'0000},
        // Past the first 1 MB, still in Tensix (1,0)'s L1 slot.
        ResolveCase{0, {1, 0}, CoreType::TENSIX, 0x20'0000, 0x10'40E0'0000},
        // The TSC reset register of Tensix (0,0), past L1, so through its CSR.
        ResolveCase{0, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x10'9303'3088},
        ResolveCase{1, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x11'9303'3088},
        ResolveCase{2, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x20'9303'3088},
        ResolveCase{3, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x21'9303'3088},
        ResolveCase{0, {0, 1}, CoreType::DISPATCH, 0, 0x10'8000'0000},
        ResolveCase{3, {1, 1}, CoreType::DISPATCH, 0, 0x21'8800'0000},
        ResolveCase{1, {1, 2}, CoreType::DRAM, 0x100, 0x13'4000'0100},
        ResolveCase{2, {0, 2}, CoreType::DRAM, 0x100, 0x23'0000'0100}));

// The column maps are transcriptions too, of each column's NOC2AXI tables in
// tests/att_maps/horizon_x2_columns.yaml.
class HorizonX2ColumnWindow : public ::testing::TestWithParam<std::tuple<size_t, att::WindowClass>> {};

TEST_P(HorizonX2ColumnWindow, MatchesTheNoc2AxiTables) {
    const auto [map_index, window_class] = GetParam();
    const ColumnMap& entry = COLUMN_MAPS[map_index];
    expect_window_matches(*entry.map, window_class, column_noc2axi_tables(entry.device, entry.column));
}

INSTANTIATE_TEST_SUITE_P(
    L1AndCsr,
    HorizonX2ColumnWindow,
    ::testing::Combine(
        ::testing::Values(0, 1, 2, 3), ::testing::Values(att::WindowClass::WORKER, att::WindowClass::FULL_TILE)));

// Each column device's addresses: those aether's AttConfig resolver gives for horizon_x2_columns.yaml
// through the column's NOC2AXI, and for DRAM the crossbar's dram_P range.
struct ColumnResolveCase {
    const att::MapData* map;
    tt_xy_pair core;
    CoreType type;
    uint64_t offset;
    uint64_t address;
};

class HorizonX2ColumnResolve : public ::testing::TestWithParam<ColumnResolveCase> {};

TEST_P(HorizonX2ColumnResolve, GivesTheTestbenchAddress) {
    const ColumnResolveCase& c = GetParam();
    const att::EndpointResolver resolver(*c.map);
    EXPECT_EQ(resolver.resolve(c.core, c.type, c.offset, 4), c.address);
}

INSTANTIATE_TEST_SUITE_P(
    PerColumn,
    HorizonX2ColumnResolve,
    ::testing::Values(
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN0_MAP, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x10'4005'0000},
        // Past the first 1 MB, still in Tensix (0,0)'s L1 slot.
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN0_MAP, {0, 0}, CoreType::TENSIX, 0x20'0000, 0x10'4020'0000},
        // The TSC reset register of Tensix (0,0), past L1, so through its CSR.
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN0_MAP, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x10'8303'3088},
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN0_MAP, {0, 1}, CoreType::DISPATCH, 0, 0x10'8800'0000},
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN0_MAP, {0, 2}, CoreType::ROUTER_ONLY, 0x100, 0x10'9000'0100},
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN0_MAP, {0, 2}, CoreType::DRAM, 0x100, 0x13'0000'0100},
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN1_MAP, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x11'4005'0000},
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN1_MAP, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x11'8303'3088},
        ColumnResolveCase{&att::HORIZON_X2_DEV0_COLUMN1_MAP, {0, 2}, CoreType::DRAM, 0x100, 0x13'4000'0100},
        ColumnResolveCase{&att::HORIZON_X2_DEV1_COLUMN0_MAP, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x20'4005'0000},
        ColumnResolveCase{&att::HORIZON_X2_DEV1_COLUMN0_MAP, {0, 2}, CoreType::DRAM, 0x100, 0x23'0000'0100},
        ColumnResolveCase{&att::HORIZON_X2_DEV1_COLUMN1_MAP, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x21'4005'0000},
        ColumnResolveCase{&att::HORIZON_X2_DEV1_COLUMN1_MAP, {0, 1}, CoreType::DISPATCH, 0x40, 0x21'8800'0040},
        ColumnResolveCase{&att::HORIZON_X2_DEV1_COLUMN1_MAP, {0, 2}, CoreType::DRAM, 0x100, 0x23'4000'0100}));

// Column 0's device has no core at x = 1: in the tables' frame that is column 1's Tensix.
TEST(HorizonX2Columns, RefusesTheOtherColumn) {
    const att::EndpointResolver resolver(att::HORIZON_X2_DEV0_COLUMN0_MAP);
    EXPECT_THROW(resolver.resolve({1, 0}, CoreType::TENSIX, 0x5'0000, 4), std::exception);
}
