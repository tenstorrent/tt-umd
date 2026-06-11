// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt::umd;

TEST(TestSocDescriptor, SocDescriptorSerialize) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_descriptor = umd_cluster->get_soc_descriptor(chip_id);

        std::filesystem::path file_path = soc_descriptor.serialize_to_file();
        SocDescriptor soc(
            std::make_shared<SocArchDescriptor>(file_path.string()),
            {.noc_translation_enabled = soc_descriptor.noc_translation_enabled,
             .harvesting_masks = soc_descriptor.harvesting_masks});
    }
}

TEST(TestSocDescriptor, LiteralCoordSystem) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_descriptor = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord dram = soc_descriptor.get_cores(tt::CoreType::DRAM, tt::CoordSystem::LOGICAL).front();
        CoreCoord literal_dram = CoreCoord(dram.x, dram.y);
        tt::xy_pair literal_dram_pair = tt::xy_pair(dram.x, dram.y);

        // Literal CoreCoords should pass through with no coordinate changes.
        EXPECT_EQ(literal_dram_pair, soc_descriptor.translate_chip_coord_to_translated(literal_dram));
        EXPECT_EQ(literal_dram, soc_descriptor.translate_chip_coord_to_translated_coord(literal_dram));

        // CoreCoords cannot be translated to LITERAL through CooridinateManager.
        EXPECT_ANY_THROW(soc_descriptor.translate_coord_to(dram, tt::CoordSystem::LITERAL));
        // LITERAL core coords are not persisted in CooridinateManager data structures.
        EXPECT_ANY_THROW(soc_descriptor.get_coord_at(literal_dram_pair, tt::CoordSystem::LITERAL));
        // You cannot translate from LITERAL coordinates because they are ambiguous.
        EXPECT_ANY_THROW(soc_descriptor.translate_coord_to(literal_dram, tt::CoordSystem::NOC0));
        EXPECT_ANY_THROW(
            soc_descriptor.translate_coord_to(literal_dram_pair, tt::CoordSystem::LITERAL, tt::CoordSystem::NOC0));
    }
}
