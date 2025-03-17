// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <thread>

#include "gtest/gtest.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;

TEST(SoftwareHarvesting, TensixSoftwareHarvestingAllChips) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::set<chip_id_t> target_devices = test_utils::get_target_devices();

    int num_devices = target_devices.size();
    std::unordered_map<chip_id_t, HarvestingMasks> software_harvesting_masks;

    for (auto chip : target_devices) {
        software_harvesting_masks[chip] = {0x3, 0, 0};
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(num_host_mem_ch_per_mmio_device, false, true, true, software_harvesting_masks);

    tt::ARCH arch = cluster->get_cluster_description()->get_arch(0);

    uint32_t upper_limit_num_cores;
    if (arch == tt::ARCH::WORMHOLE_B0) {
        // At least 2 rows are expected to be harvested.
        upper_limit_num_cores = 64;
    } else if (arch == tt::ARCH::BLACKHOLE) {
        // At least 2 columns are expected to be harvested.
        upper_limit_num_cores = 120;
    }

    for (const chip_id_t& chip : cluster->get_target_device_ids()) {
        ASSERT_LE(cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX).size(), upper_limit_num_cores);
    }

    for (const chip_id_t& chip : cluster->get_target_device_ids()) {
        EXPECT_TRUE(
            (software_harvesting_masks.at(chip).tensix_harvesting_mask &
             cluster->get_soc_descriptor(chip).harvesting_masks.tensix_harvesting_mask) ==
            software_harvesting_masks.at(chip).tensix_harvesting_mask);
    }
}
