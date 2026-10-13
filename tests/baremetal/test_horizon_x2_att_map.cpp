// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/coordinates/att/att_map.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/att_window.hpp"
#include "tt-umd/coordinates/att/configs/horizon_x2_att_map.hpp"
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

}  // namespace

// The maps are transcriptions, so they are checked against the tables they were transcribed from:
// tests/att_maps/horizon_x2_per_noc2axi.yaml.
class HorizonX2RebasedWindow : public ::testing::TestWithParam<std::tuple<size_t, att::WindowClass>> {};

TEST_P(HorizonX2RebasedWindow, MatchesTheTables) {
    const auto [access_point, window_class] = GetParam();
    const AccessPointMap& entry = ACCESS_POINT_MAPS[access_point];
    const YAML::Node tables = device_tables(entry.device);
    const auto index = static_cast<size_t>(window_class);
    const att::Window& window = entry.map->windows[index];

    const YAML::Node mask_entry = mask_entry_at(tables, window.compare);
    ASSERT_TRUE(mask_entry.IsDefined()) << "no mask entry at 0x" << std::hex << window.compare;
    EXPECT_EQ(window.mask_bits, mask_entry["mask"].as<uint32_t>());
    EXPECT_EQ(window.endpoint_shift, mask_entry["ep_idx"].as<uint32_t>());
    EXPECT_EQ(window.endpoint_size, mask_entry["ep_id_size"].as<uint32_t>());
    EXPECT_EQ(window.endpoint_table_offset, mask_entry["table_offset"].as<uint32_t>());
    EXPECT_EQ(window.translate_address, mask_entry["translate_addr"].as<int>() == 1);

    const att::Table<uint16_t>& words = entry.map->endpoint_words[index];
    for (uint32_t selector = 0; selector < words.size(); selector++) {
        EXPECT_EQ(words[selector], row_word(tables, window.endpoint_table_offset + selector))
            << "selector " << selector;
    }
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
        ResolveCase{0, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x10'4025'0000},
        ResolveCase{1, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x11'4025'0000},
        ResolveCase{2, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x20'4025'0000},
        ResolveCase{3, {0, 0}, CoreType::TENSIX, 0x5'0000, 0x21'4025'0000},
        // The TSC reset register of Tensix (0,0), past L1, so through its CSR.
        ResolveCase{0, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x10'9303'3088},
        ResolveCase{1, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x11'9303'3088},
        ResolveCase{2, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x20'9303'3088},
        ResolveCase{3, {0, 0}, CoreType::TENSIX, 0x0303'3088, 0x21'9303'3088},
        ResolveCase{0, {1, 0}, CoreType::TENSIX, 0x10'0000, 0x10'9810'0000},
        ResolveCase{0, {0, 1}, CoreType::DISPATCH, 0, 0x10'8000'0000},
        ResolveCase{3, {1, 1}, CoreType::DISPATCH, 0, 0x21'8800'0000},
        ResolveCase{1, {1, 2}, CoreType::DRAM, 0x100, 0x13'4000'0100},
        ResolveCase{2, {0, 2}, CoreType::DRAM, 0x100, 0x23'0000'0100}));
