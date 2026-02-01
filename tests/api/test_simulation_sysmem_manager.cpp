// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <sys/mman.h>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

const uint32_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

TEST(ApiSimulationSysmemManager, BasicIOSingleChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem = std::make_unique<SimulationSysmemManager>(1);

    const HugepageMapping channel_0 = sysmem->get_hugepage_mapping(0);

    EXPECT_EQ(channel_0.mapping_size, HUGEPAGE_REGION_SIZE);

    void* channel_0_mapping = channel_0.mapping;

    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(0, data_write.data(), 0, data_write.size());

    std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
    sysmem->read_from_sysmem(0, data_read.data(), 0, data_read.size());

    EXPECT_EQ(data_write, data_read);

    for (int i = 0; i < data_write.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t*>(channel_0_mapping)[i], data_write[i]);
    }
}

TEST(ApiSimulationSysmemManager, BasicIOMultiChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem = std::make_unique<SimulationSysmemManager>(3);

    for (int i = 0; i < 3; i++) {
        const HugepageMapping channel = sysmem->get_hugepage_mapping(i);

        EXPECT_EQ(channel.mapping_size, HUGEPAGE_REGION_SIZE);

        void* channel_mapping = channel.mapping;

        std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

        sysmem->write_to_sysmem(i, data_write.data(), 0, data_write.size());

        std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
        sysmem->read_from_sysmem(i, data_read.data(), 0, data_read.size());

        EXPECT_EQ(data_write, data_read);

        for (int j = 0; j < data_write.size(); j++) {
            EXPECT_EQ(static_cast<uint8_t*>(channel_mapping)[j], data_write[j]);
        }
    }
}

TEST(ApiSimulationSysmemManager, TestFourChannels) {
    std::unique_ptr<SimulationSysmemManager> sysmem = std::make_unique<SimulationSysmemManager>(4);

    const HugepageMapping channel_3 = sysmem->get_hugepage_mapping(3);

    EXPECT_EQ(channel_3.mapping_size, HUGEPAGE_CHANNEL_3_SIZE_LIMIT);

    void* channel_3_mapping = channel_3.mapping;

    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(3, data_write.data(), 0, data_write.size());

    std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
    sysmem->read_from_sysmem(3, data_read.data(), 0, data_read.size());

    EXPECT_EQ(data_write, data_read);

    for (int i = 0; i < data_write.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t*>(channel_3_mapping)[i], data_write[i]);
    }
}
