// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

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
