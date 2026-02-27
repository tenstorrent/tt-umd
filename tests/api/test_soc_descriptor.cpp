// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "umd/device/cluster.hpp"
#include "utils.hpp"

using namespace tt::umd;

TEST(TestSocDescriptor, SocDescriptorSerialize) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_descriptor = umd_cluster->get_soc_descriptor(chip_id);

        std::filesystem::path file_path = soc_descriptor.serialize_to_file();
        SocDescriptor soc(
            file_path.string(),
            {.noc_translation_enabled = soc_descriptor.noc_translation_enabled,
             .harvesting_masks = soc_descriptor.harvesting_masks});
    }
}
