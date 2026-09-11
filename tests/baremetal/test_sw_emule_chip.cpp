// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/chip/sw_emule_chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt;
using namespace tt::umd;

TEST(ApiEmuleClusterTest, CreateEmuleSingleChipClusters) {
    // SWEmuleChip only supports single-chip clusters. Test the two single-chip descriptors:
    // - wormhole_N150.yaml (1 chip)
    // - wormhole_N150_unique_ids.yaml (1 chip, exercises chip_unique_ids parsing path)
    const std::vector<std::string> single_chip_descs = {
        test_utils::GetClusterDescAbsPath("wormhole_N150.yaml"),
        test_utils::GetClusterDescAbsPath("wormhole_N150_unique_ids.yaml"),
    };

    for (const auto& descriptor_file : single_chip_descs) {
        log_info(LogUMD, "Testing emule cluster creation from: {}", descriptor_file);
        std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(descriptor_file);
        ASSERT_NE(cluster_desc, nullptr) << "Cluster descriptor is null for: " << descriptor_file;
        ASSERT_FALSE(cluster_desc->get_all_chips().empty()) << "Cluster descriptor has no chips: " << descriptor_file;
        EXPECT_GT(cluster_desc->get_chips_grouped_by_closest_mmio().size(), 0);

        auto emule_cluster = std::make_unique<Cluster>(
            ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
        ASSERT_NE(emule_cluster, nullptr) << "Emule cluster is null for: " << descriptor_file;

        std::vector<uint8_t> data(1024, 0);
        for (auto chip_id : emule_cluster->get_target_device_ids()) {
            CoreCoord any_tensix_core = emule_cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)[0];
            emule_cluster->write_to_device(data.data(), data.size(), chip_id, any_tensix_core, 0);
            emule_cluster->read_from_device(data.data(), chip_id, any_tensix_core, 0, data.size());
        }
    }
}

TEST(ApiEmuleClusterTest, EmuleRoundtripIO) {
    std::string descriptor_file = test_utils::GetClusterDescAbsPath("wormhole_N150.yaml");
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(descriptor_file);
    ASSERT_NE(cluster_desc, nullptr);

    auto emule_cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});

    ChipId chip_id = *emule_cluster->get_target_device_ids().begin();
    const auto& tensix_cores = emule_cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

    constexpr size_t kDataSize = 1024;
    constexpr uint64_t kOffsets[] = {0, 4096, 8192};

    for (const auto& core : tensix_cores) {
        for (uint64_t offset : kOffsets) {
            // Fill with a pattern unique to this core + offset.
            std::vector<uint8_t> write_data(kDataSize);
            uint8_t seed = static_cast<uint8_t>(core.x ^ core.y ^ (offset >> 12));
            for (size_t i = 0; i < kDataSize; ++i) {
                write_data[i] = static_cast<uint8_t>(seed + i);
            }

            emule_cluster->write_to_device(write_data.data(), write_data.size(), chip_id, core, offset);

            std::vector<uint8_t> read_data(kDataSize, 0);
            emule_cluster->read_from_device(read_data.data(), chip_id, core, offset, read_data.size());

            EXPECT_EQ(write_data, read_data)
                << "Roundtrip mismatch at core (" << core.x << "," << core.y << ") offset " << offset;
        }
    }
}

// The slot map is the premise of shared chip backing: a rank resolves a fabric write into a chip it
// does not own by deriving that chip's layout from the SoC descriptor alone. These pin the
// properties that makes true, none of which needs shared memory or a second process.

TEST(ApiEmuleClusterTest, WorkerSlotMapIsDeterministicAndDense) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_N150.yaml"));
    auto cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
    ChipId chip_id = *cluster->get_target_device_ids().begin();
    const SocDescriptor& soc = cluster->get_soc_descriptor(chip_id);

    // Two independent builds from one descriptor must agree — this stands in for two PROCESSES,
    // which is what a shared segment actually requires.
    const auto a = build_worker_slot_map(soc);
    const auto b = build_worker_slot_map(soc);
    EXPECT_EQ(a, b);

    const auto tensix = soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    ASSERT_FALSE(tensix.empty());
    EXPECT_EQ(a.size(), tensix.size());

    // Dense and collision-free: exactly {0 .. n-1}. A duplicate would alias two cores onto one
    // slot; a gap would waste a slot in the scarce low-4 GB window.
    std::set<size_t> slots;
    for (const auto& core : tensix) {
        auto it = a.find(tt_xy_pair(core.x, core.y));
        ASSERT_NE(it, a.end()) << "translated core (" << core.x << "," << core.y << ") has no slot";
        EXPECT_TRUE(slots.insert(it->second).second) << "slot " << it->second << " assigned twice";
    }
    EXPECT_EQ(*slots.begin(), 0u);
    EXPECT_EQ(*slots.rbegin(), tensix.size() - 1);
}

TEST(ApiEmuleClusterTest, PoolIsSizedToTensixCount) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_N150.yaml"));
    auto cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
    ChipId chip_id = *cluster->get_target_device_ids().begin();

    auto* chip = dynamic_cast<SWEmuleChip*>(cluster->get_chip(chip_id));
    ASSERT_NE(chip, nullptr);
    EXPECT_EQ(
        chip->num_pool_slots(),
        cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED).size());
}

TEST(ApiEmuleClusterTest, NonTranslatedCoreNamingStillResolvesToAPoolSlot) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_N150.yaml"));
    auto cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
    ChipId chip_id = *cluster->get_target_device_ids().begin();
    const SocDescriptor& soc = cluster->get_soc_descriptor(chip_id);
    auto* chip = dynamic_cast<SWEmuleChip*>(cluster->get_chip(chip_id));
    ASSERT_NE(chip, nullptr);

    // get_cores() defaults to NOC0, which is what a caller naturally reaches for, and on Wormhole
    // NOC0 Tensix (x <= 9) are DISJOINT from TRANSLATED (from 18). A write addressed in one naming
    // must be visible when read back in the other -- a tile's names share one L1 on silicon, so two
    // encodings of the same worker must not resolve to two separate backings.
    const auto noc0_cores = soc.get_cores(CoreType::TENSIX, CoordSystem::NOC0);
    const auto translated_cores = soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    ASSERT_EQ(noc0_cores.size(), translated_cores.size());

    for (size_t i = 0; i < noc0_cores.size(); ++i) {
        EXPECT_TRUE(chip->slot_of(tt_xy_pair(translated_cores[i].x, translated_cores[i].y)).has_value());

        const std::vector<uint8_t> written(64, static_cast<uint8_t>(i + 1));
        cluster->write_to_device(written.data(), written.size(), chip_id, noc0_cores[i], /*addr=*/0);

        std::vector<uint8_t> read_back(written.size(), 0);
        cluster->read_from_device(read_back.data(), chip_id, translated_cores[i], /*addr=*/0, read_back.size());
        EXPECT_EQ(written, read_back) << "NOC0 core (" << noc0_cores[i].x << "," << noc0_cores[i].y
                                      << ") and translated core (" << translated_cores[i].x << ","
                                      << translated_cores[i].y << ") resolved to different backings";
    }
}

TEST(ApiEmuleClusterTest, RefusesDescriptorWhoseL1ExceedsTheSlotStride) {
    // quasar_simulation_1x1.yaml declares worker_l1_size 4 MiB, above the 2 MB slot stride. Before
    // the slot map was fixed at construction this silently overran into the next core's slot.
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::QUASAR, /*noc_translation_enabled=*/true);
    auto build_oversized_l1_cluster = [&]() {
        ClusterOptions options{};
        options.chip_type = ChipType::SWEMULE;
        options.sdesc_path = test_utils::GetSocDescAbsPath("quasar_simulation_1x1.yaml");
        options.cluster_descriptor = cluster_desc.get();
        Cluster cluster{options};
    };
    EXPECT_THROW(build_oversized_l1_cluster(), std::exception);
}

TEST(ApiEmuleClusterTest, SharedBackingRefusesASynthesizedUniqueId) {
    // wormhole_N150.yaml carries no chip_unique_ids block, so the descriptor synthesizes chip << 32
    // -- stable within a process, but it names no physical chip. Two ranks each holding a different
    // chip 0 would compute the same segment key, so sharing must refuse it rather than collide.
    std::unique_ptr<ClusterDescriptor> synthesized =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_N150.yaml"));
    EXPECT_FALSE(synthesized->has_authentic_chip_unique_ids());

    std::unique_ptr<ClusterDescriptor> authentic =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_N150_unique_ids.yaml"));
    EXPECT_TRUE(authentic->has_authentic_chip_unique_ids());

    // Mock descriptors synthesize too, and their ids are bare logical ids -- the worst case.
    std::unique_ptr<ClusterDescriptor> mock =
        ClusterDescriptor::create_mock_cluster({0, 1}, tt::ARCH::WORMHOLE_B0, /*noc_translation_enabled=*/true);
    EXPECT_FALSE(mock->has_authentic_chip_unique_ids());
}
