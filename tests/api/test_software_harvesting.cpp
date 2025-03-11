// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <thread>

#include "gtest/gtest.h"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;

std::set<chip_id_t> get_target_devices() {
    std::set<chip_id_t> target_devices;
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc_uniq = Cluster::create_cluster_descriptor();
    for (int i = 0; i < cluster_desc_uniq->get_number_of_chips(); i++) {
        target_devices.insert(i);
    }
    return target_devices;
}

static uint32_t get_expected_upper_limit_tensix_number(tt::ARCH arch, uint32_t sw_harvesting_mask) {
    uint32_t harvested_rows_or_columns = CoordinateManager::get_num_harvested(sw_harvesting_mask);

    uint32_t upper_limit;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            upper_limit = tt::umd::wormhole::TENSIX_CORES.size() -
                          tt::umd::wormhole::TENSIX_GRID_SIZE.x * harvested_rows_or_columns;
            break;
        }
        case tt::ARCH::BLACKHOLE: {
            upper_limit = tt::umd::blackhole::TENSIX_CORES.size() -
                          tt::umd::blackhole::TENSIX_GRID_SIZE.y * harvested_rows_or_columns;
            break;
        }
        default: {
            throw std::runtime_error("Invalid architecture in test for software harvesting mask.");
        }
    }

    return upper_limit;
}

TEST(SoftwareHarvesting, TensixSoftwareHarvestingAllChips) {
    std::set<chip_id_t> target_devices = get_target_devices();
    int num_devices = target_devices.size();
    std::unordered_map<chip_id_t, HarvestingMasks> software_harvesting_masks;

    for (auto chip : target_devices) {
        software_harvesting_masks[chip] = {0x3, 0, 0};
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(num_host_mem_ch_per_mmio_device, false, true, true, software_harvesting_masks);

    tt::ARCH arch = cluster->get_cluster_description()->get_arch(0);

    for (const chip_id_t& chip : cluster->get_target_device_ids()) {
        ASSERT_LE(
            cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX).size(),
            get_expected_upper_limit_tensix_number(
                arch, cluster->get_soc_descriptor(chip).harvesting_masks.tensix_harvesting_mask));
    }

    for (const chip_id_t& chip : cluster->get_target_device_ids()) {
        EXPECT_TRUE(
            (software_harvesting_masks.at(chip).tensix_harvesting_mask &
             cluster->get_soc_descriptor(chip).harvesting_masks.tensix_harvesting_mask) ==
            software_harvesting_masks.at(chip).tensix_harvesting_mask);
    }
}
