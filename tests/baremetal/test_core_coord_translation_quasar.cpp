// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/common.hpp"

using namespace tt;
using namespace tt::umd;

// QuasarCoordinateManager currently maps every NOC0 coordinate to itself in
// the TRANSLATED system (see #2494). These tests assert that identity mapping
// for every core type the manager handles, plus the logical/NOC0/NOC1 and
// harvesting plumbing inherited from CoordinateManager.

namespace {

// Checks that NOC0 -> TRANSLATED is an identity on (x, y). The placeholder
// Quasar arch constants have overlapping NOC0 entries across core types
// (e.g. {9, 2} is in both TENSIX_CORES_NOC0 and DRAM_CORES_NOC0), so the
// looked-up TRANSLATED core_type is not guaranteed to match the requested
// one — but the coordinate identity is what QuasarCoordinateManager actually
// promises.
void expect_identity_translated_for_cores(
    const std::shared_ptr<CoordinateManager>& cm, const std::vector<tt_xy_pair>& cores_noc0, CoreType core_type) {
    for (const tt_xy_pair& core_noc0 : cores_noc0) {
        const CoreCoord noc0(core_noc0.x, core_noc0.y, core_type, CoordSystem::NOC0);
        const CoreCoord translated = cm->translate_coord_to(noc0, CoordSystem::TRANSLATED);
        EXPECT_EQ(translated.x, core_noc0.x);
        EXPECT_EQ(translated.y, core_noc0.y);
        EXPECT_EQ(translated.coord_system, CoordSystem::TRANSLATED);
    }
}

}  // namespace

// With no harvesting, the logical grid covers all TENSIX_GRID_SIZE cores in
// row-major order and every NOC0 coordinate maps to itself in TRANSLATED.
TEST(CoordinateManager, CoordinateManagerQuasarNoHarvesting) {
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true);

    const tt_xy_pair grid = grendel::TENSIX_GRID_SIZE;
    EXPECT_EQ(cm->get_grid_size(CoreType::TENSIX).x, grid.x);
    EXPECT_EQ(cm->get_grid_size(CoreType::TENSIX).y, grid.y);

    for (size_t y = 0; y < grid.y; y++) {
        for (size_t x = 0; x < grid.x; x++) {
            const CoreCoord logical(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            const CoreCoord noc0 = cm->translate_coord_to(logical, CoordSystem::NOC0);
            const CoreCoord translated = cm->translate_coord_to(logical, CoordSystem::TRANSLATED);
            const tt_xy_pair expected_noc0 = grendel::TENSIX_CORES_NOC0[y * grid.x + x];

            EXPECT_EQ(noc0.x, expected_noc0.x);
            EXPECT_EQ(noc0.y, expected_noc0.y);
            EXPECT_EQ(translated.x, noc0.x);
            EXPECT_EQ(translated.y, noc0.y);
        }
    }
}

// Harvesting the first NOC0 row shifts the top-left logical core onto the
// second NOC0 row of TENSIX_CORES_NOC0.
TEST(CoordinateManager, CoordinateManagerQuasarTopLeftCoreHarvested) {
    const size_t tensix_harvesting_mask = (1 << 0);
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(
        tt::ARCH::QUASAR, true, {.tensix_harvesting_mask = tensix_harvesting_mask});

    const tt_xy_pair grid = cm->get_grid_size(CoreType::TENSIX);
    EXPECT_EQ(grid.x, grendel::TENSIX_GRID_SIZE.x);
    EXPECT_EQ(grid.y, grendel::TENSIX_GRID_SIZE.y - 1);

    const CoreCoord logical(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);
    const CoreCoord noc0 = cm->translate_coord_to(logical, CoordSystem::NOC0);
    const tt_xy_pair expected_noc0 = grendel::TENSIX_CORES_NOC0[grendel::TENSIX_GRID_SIZE.x];
    EXPECT_EQ(noc0.x, expected_noc0.x);
    EXPECT_EQ(noc0.y, expected_noc0.y);

    const CoreCoord translated = cm->translate_coord_to(logical, CoordSystem::TRANSLATED);
    EXPECT_EQ(translated.x, noc0.x);
    EXPECT_EQ(translated.y, noc0.y);
}

// Logical <-> NOC0 must be a bijection on the unharvested grid for every
// possible tensix harvesting mask.
TEST(CoordinateManager, CoordinateManagerQuasarLogicalNOC0Mapping) {
    const size_t num_rows = grendel::TENSIX_GRID_SIZE.y;

    for (size_t harvesting = 0; harvesting < (1u << num_rows); harvesting++) {
        std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(
            tt::ARCH::QUASAR, true, {.tensix_harvesting_mask = harvesting});

        const size_t num_harvested = CoordinateManager::get_num_harvested(harvesting);
        const tt_xy_pair grid = grendel::TENSIX_GRID_SIZE;

        std::set<CoreCoord> noc0_seen;
        for (size_t y = 0; y < grid.y - num_harvested; y++) {
            for (size_t x = 0; x < grid.x; x++) {
                const CoreCoord logical(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
                const CoreCoord noc0 = cm->translate_coord_to(logical, CoordSystem::NOC0);
                EXPECT_EQ(noc0_seen.count(noc0), 0u);
                noc0_seen.insert(noc0);

                const CoreCoord back = cm->translate_coord_to(noc0, CoordSystem::LOGICAL);
                EXPECT_EQ(back, logical);
            }
        }
        EXPECT_EQ(noc0_seen.size(), grid.x * (grid.y - num_harvested));
    }
}

// Even rows hidden from the logical grid by tensix harvesting still get an
// identity TRANSLATED entry, since Quasar's fill_tensix_noc0_translated_mapping
// delegates to the base-class identity default.
TEST(CoordinateManager, CoordinateManagerQuasarTensixTranslatedMappingHarvested) {
    const size_t tensix_harvesting_mask = (1 << 0) | (1 << 2);
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(
        tt::ARCH::QUASAR, true, {.tensix_harvesting_mask = tensix_harvesting_mask});

    expect_identity_translated_for_cores(cm, grendel::TENSIX_CORES_NOC0, CoreType::TENSIX);
}

// DRAM, ETH, ARC, PCIE all use identity NOC0 -> TRANSLATED on Quasar.
TEST(CoordinateManager, CoordinateManagerQuasarIdentityTranslatedNonTensix) {
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true);

    expect_identity_translated_for_cores(cm, grendel::DRAM_LOCATIONS, CoreType::DRAM);
    expect_identity_translated_for_cores(cm, grendel::ETH_CORES_NOC0, CoreType::ETH);
    expect_identity_translated_for_cores(cm, grendel::ARC_CORES_NOC0, CoreType::ARC);
    expect_identity_translated_for_cores(cm, grendel::PCIE_CORES_NOC0, CoreType::PCIE);
}

// DRAM logical(channel, subchannel) -> NOC0 follows grendel::DRAM_CORES_NOC0.
TEST(CoordinateManager, CoordinateManagerQuasarDRAMLogicalNOC0Mapping) {
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true);

    for (size_t ch = 0; ch < grendel::DRAM_CORES_NOC0.size(); ch++) {
        for (size_t sub = 0; sub < grendel::DRAM_CORES_NOC0[ch].size(); sub++) {
            const CoreCoord logical(ch, sub, CoreType::DRAM, CoordSystem::LOGICAL);
            const CoreCoord noc0 = cm->translate_coord_to(logical, CoordSystem::NOC0);
            EXPECT_EQ(noc0.x, grendel::DRAM_CORES_NOC0[ch][sub].x);
            EXPECT_EQ(noc0.y, grendel::DRAM_CORES_NOC0[ch][sub].y);
        }
    }
}

// ETH logical(0, channel) -> NOC0 follows grendel::ETH_CORES_NOC0.
TEST(CoordinateManager, CoordinateManagerQuasarETHLogicalNOC0Mapping) {
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true);

    for (size_t ch = 0; ch < grendel::ETH_CORES_NOC0.size(); ch++) {
        const CoreCoord logical(0, ch, CoreType::ETH, CoordSystem::LOGICAL);
        const CoreCoord noc0 = cm->translate_coord_to(logical, CoordSystem::NOC0);
        EXPECT_EQ(noc0.x, grendel::ETH_CORES_NOC0[ch].x);
        EXPECT_EQ(noc0.y, grendel::ETH_CORES_NOC0[ch].y);
    }
}

// DRAM harvesting is rejected by the base CoordinateManager outside Blackhole.
TEST(CoordinateManager, CoordinateManagerQuasarDRAMHarvestingThrows) {
    EXPECT_THROW(
        CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true, {.dram_harvesting_mask = 0x1}),
        std::runtime_error);
}

// ETH harvesting is rejected by the base CoordinateManager outside Blackhole.
TEST(CoordinateManager, CoordinateManagerQuasarETHHarvestingThrows) {
    EXPECT_THROW(
        CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true, {.eth_harvesting_mask = 0x1}),
        std::runtime_error);
}

// NOC0 <-> NOC1 round-trip uses grendel::NOC0_X_TO_NOC1_X / NOC0_Y_TO_NOC1_Y.
TEST(CoordinateManager, CoordinateManagerQuasarNoc1Noc0Mapping) {
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, true);

    auto check_round_trip = [&cm](const std::vector<tt_xy_pair>& cores, CoreType core_type) {
        for (const tt_xy_pair& core_noc0 : cores) {
            const CoreCoord noc0(core_noc0.x, core_noc0.y, core_type, CoordSystem::NOC0);
            const CoreCoord noc1 = cm->translate_coord_to(noc0, CoordSystem::NOC1);
            EXPECT_EQ(noc1.x, grendel::NOC0_X_TO_NOC1_X[core_noc0.x]);
            EXPECT_EQ(noc1.y, grendel::NOC0_Y_TO_NOC1_Y[core_noc0.y]);

            const CoreCoord noc0_back = cm->translate_coord_to(noc1, CoordSystem::NOC0);
            EXPECT_EQ(noc0_back.x, core_noc0.x);
            EXPECT_EQ(noc0_back.y, core_noc0.y);
        }
    };

    check_round_trip(grendel::TENSIX_CORES_NOC0, CoreType::TENSIX);
    check_round_trip(grendel::DRAM_LOCATIONS, CoreType::DRAM);
    check_round_trip(grendel::ETH_CORES_NOC0, CoreType::ETH);
    check_round_trip(grendel::ARC_CORES_NOC0, CoreType::ARC);
    check_round_trip(grendel::PCIE_CORES_NOC0, CoreType::PCIE);
}

// With noc_translation_enabled=false the base manager still installs the
// identity NOC0 -> TRANSLATED defaults, so TRANSLATED lookups stay valid.
TEST(CoordinateManager, CoordinateManagerQuasarNoNocTranslation) {
    std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(tt::ARCH::QUASAR, false);

    expect_identity_translated_for_cores(cm, grendel::TENSIX_CORES_NOC0, CoreType::TENSIX);
    expect_identity_translated_for_cores(cm, grendel::DRAM_LOCATIONS, CoreType::DRAM);
    expect_identity_translated_for_cores(cm, grendel::ETH_CORES_NOC0, CoreType::ETH);
    expect_identity_translated_for_cores(cm, grendel::ARC_CORES_NOC0, CoreType::ARC);
    expect_identity_translated_for_cores(cm, grendel::PCIE_CORES_NOC0, CoreType::PCIE);
}

// get_harvesting_masks should round-trip whatever tensix mask was passed in.
TEST(CoordinateManager, CoordinateManagerQuasarHarvestingMaskRoundTrip) {
    const size_t num_rows = grendel::TENSIX_GRID_SIZE.y;
    for (size_t harvesting = 0; harvesting < (1u << num_rows); harvesting++) {
        std::shared_ptr<CoordinateManager> cm = CoordinateManager::create_coordinate_manager(
            tt::ARCH::QUASAR, true, {.tensix_harvesting_mask = harvesting});
        EXPECT_EQ(cm->get_harvesting_masks().tensix_harvesting_mask, harvesting);
    }
}
