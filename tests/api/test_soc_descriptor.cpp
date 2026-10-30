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
#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/cluster.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

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

        // Literal CoreCoords should pass through with no coordinate changes, on either NOC.
        for (const NocId noc_id : {NocId::NOC0, NocId::NOC1}) {
            EXPECT_EQ(literal_dram_pair, soc_descriptor.translate_chip_coord_to_translated(literal_dram, noc_id));
            EXPECT_EQ(literal_dram, soc_descriptor.translate_chip_coord_to_translated_coord(literal_dram, noc_id));
        }

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

// A Mimir chiplet carries DRAM and the SMC, and no compute. These tests need no device: they only
// exercise the descriptor.
TEST(TestSocDescriptor, MimirDescriptorEnumeratesDramAndSmcCores) {
    SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_1x1.yaml")));

    EXPECT_EQ(soc_descriptor.get_arch_descriptor().get_arch(), tt::ARCH::QUASAR);

    // Two DRAM cores, one per chippy GDDR instance.
    EXPECT_EQ(soc_descriptor.get_cores(tt::CoreType::DRAM).size(), 2);

    std::vector<CoreCoord> smc_cores = soc_descriptor.get_cores(tt::CoreType::SMC);
    ASSERT_EQ(smc_cores.size(), 1);
    EXPECT_EQ(smc_cores.front().x, 0);
    EXPECT_EQ(smc_cores.front().y, 1);
    EXPECT_EQ(smc_cores.front().core_type, tt::CoreType::SMC);

    // Mimir has no compute or ethernet.
    EXPECT_TRUE(soc_descriptor.get_cores(tt::CoreType::TENSIX).empty());
    EXPECT_TRUE(soc_descriptor.get_cores(tt::CoreType::ETH).empty());

    // Like the other irregular core types, SMC cores are never harvested and have no grid.
    EXPECT_TRUE(soc_descriptor.get_harvested_cores(tt::CoreType::SMC).empty());
    EXPECT_EQ(soc_descriptor.get_grid_size(tt::CoreType::SMC), tt_xy_pair(0, 0));

    // The SMC core is reachable through the coordinate manager, not just the type query.
    EXPECT_EQ(soc_descriptor.get_coord_at(tt_xy_pair(0, 1), tt::CoordSystem::NOC0).core_type, tt::CoreType::SMC);

    std::vector<CoreCoord> all_cores = soc_descriptor.get_all_cores();
    EXPECT_EQ(all_cores.size(), 3);
}

TEST(TestSocDescriptor, SmcCoresSurviveSerializationRoundTrip) {
    SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_1x1.yaml")));

    std::filesystem::path file_path = soc_descriptor.serialize_to_file();
    SocDescriptor reloaded(std::make_shared<SocArchDescriptor>(file_path.string()));

    EXPECT_EQ(reloaded.get_cores(tt::CoreType::SMC).size(), soc_descriptor.get_cores(tt::CoreType::SMC).size());
    EXPECT_EQ(reloaded.get_cores(tt::CoreType::SMC).front(), soc_descriptor.get_cores(tt::CoreType::SMC).front());
}

TEST(TestSocDescriptor, DualMimirPackageEnumeratesOnlyManagementCores) {
    SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_2x_package.yaml")));

    EXPECT_EQ(soc_descriptor.get_arch_descriptor().get_arch(), tt::ARCH::QUASAR);
    EXPECT_EQ(soc_descriptor.get_num_dram_channels(), 2);
    EXPECT_EQ(soc_descriptor.get_cores(tt::CoreType::DRAM).size(), 4);

    const std::vector<CoreCoord> smc_cores = soc_descriptor.get_cores(tt::CoreType::SMC, tt::CoordSystem::NOC0);
    ASSERT_EQ(smc_cores.size(), 2);
    EXPECT_EQ(smc_cores[0], CoreCoord(0, 1, tt::CoreType::SMC, tt::CoordSystem::NOC0));
    EXPECT_EQ(smc_cores[1], CoreCoord(1, 1, tt::CoreType::SMC, tt::CoordSystem::NOC0));

    for (uint32_t channel = 0; channel < 2; ++channel) {
        for (uint32_t location = 0; location < 2; ++location) {
            const CoreCoord logical =
                soc_descriptor.get_dram_core_for_channel(channel, location, tt::CoordSystem::LOGICAL);
            EXPECT_EQ(logical.x, channel);
            EXPECT_EQ(logical.y, location);
        }
    }

    EXPECT_TRUE(soc_descriptor.get_cores(tt::CoreType::TENSIX).empty());
    EXPECT_TRUE(soc_descriptor.get_cores(tt::CoreType::ETH).empty());
    EXPECT_TRUE(soc_descriptor.get_cores(tt::CoreType::ARC).empty());
    EXPECT_EQ(soc_descriptor.get_all_cores().size(), 6);
}

TEST(TestSocDescriptor, MmkKeraunosChipletSurvivesSerialization) {
    SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mmk.yaml")));
    const KeraunosChiplet& keraunos = soc_descriptor.get_arch_descriptor().get_keraunos();
    EXPECT_EQ(keraunos.chiplet, "k0");
    ASSERT_EQ(soc_descriptor.get_cores(tt::CoreType::ETH).size(), 8u);

    const std::filesystem::path file_path = soc_descriptor.serialize_to_file();
    SocDescriptor reloaded(std::make_shared<SocArchDescriptor>(file_path.string()));
    const KeraunosChiplet& reloaded_keraunos = reloaded.get_arch_descriptor().get_keraunos();
    EXPECT_EQ(reloaded_keraunos.chiplet, keraunos.chiplet);
    EXPECT_EQ(reloaded_keraunos.tap, keraunos.tap);
    EXPECT_EQ(reloaded_keraunos.local_window, keraunos.local_window);
    EXPECT_EQ(reloaded_keraunos.tile_stride, keraunos.tile_stride);
    ASSERT_EQ(reloaded_keraunos.cores.size(), keraunos.cores.size());
    EXPECT_EQ(reloaded_keraunos.cores[0].coord, keraunos.cores[0].coord);
    EXPECT_EQ(reloaded_keraunos.cores[0].hsio, keraunos.cores[0].hsio);
    EXPECT_EQ(reloaded_keraunos.cores[1].cce, keraunos.cores[1].cce);
    EXPECT_EQ(reloaded_keraunos.cores.back().hsio, keraunos.cores.back().hsio);
    EXPECT_EQ(reloaded_keraunos.cores.back().cce, keraunos.cores.back().cce);
    EXPECT_EQ(reloaded.get_cores(tt::CoreType::ETH).size(), 8u);
    EXPECT_EQ(reloaded.get_arch_descriptor().get_mimir_channels().size(), 2u);
}
