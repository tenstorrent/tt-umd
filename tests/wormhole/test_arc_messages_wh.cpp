// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
using namespace tt::umd;

TEST(WormholeArcMessages, WormholeArcMessagesHarvesting) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    for (uint32_t chip_id : cluster->get_target_mmio_device_ids()) {
        TTDevice* tt_device = cluster->get_tt_device(chip_id);

        auto harvesting_mask_cluster_desc = cluster->get_cluster_description()->get_harvesting_masks(chip_id);

        std::unique_ptr<ArcMessenger> arc_messenger = ArcMessenger::create_arc_messenger(tt_device);

        std::vector<uint32_t> arc_msg_return_values = {0};
        uint32_t response = arc_messenger->send_message(
            wormhole::ARC_MSG_COMMON_PREFIX |
                tt_device->get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
            arc_msg_return_values,
            {0, 0});

        EXPECT_EQ(
            CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, arc_msg_return_values[0]),
            harvesting_mask_cluster_desc.tensix_harvesting_mask);
    }
}

TEST(WormholeArcMessages, WormholeArcMessagesAICLK) {
    const uint32_t ms_sleep = 2000;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    for (uint32_t chip_id : cluster->get_target_mmio_device_ids()) {
        TTDevice* tt_device = cluster->get_tt_device(chip_id);

        std::unique_ptr<ArcMessenger> arc_messenger = ArcMessenger::create_arc_messenger(tt_device);

        uint32_t response = arc_messenger->send_message(
            wormhole::ARC_MSG_COMMON_PREFIX |
                tt_device->get_architecture_implementation()->get_arc_message_arc_go_busy(),
            {0, 0});

        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));

        uint32_t aiclk = tt_device->get_clock();

        // TODO #781: For now expect only that busy val is something larger than idle val.
        EXPECT_GT(aiclk, wormhole::AICLK_IDLE_VAL);

        response = arc_messenger->send_message(
            wormhole::ARC_MSG_COMMON_PREFIX |
                tt_device->get_architecture_implementation()->get_arc_message_arc_go_long_idle(),
            {0, 0});

        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));

        aiclk = tt_device->get_clock();

        EXPECT_EQ(aiclk, wormhole::AICLK_IDLE_VAL);
    }
}

TEST(WormholeArcMessages, MultipleThreadsArcMessages) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const uint32_t num_loops = 1000;

    for (uint32_t chip_id : cluster->get_target_mmio_device_ids()) {
        TTDevice* tt_device = cluster->get_tt_device(chip_id);

        auto harvesting_mask_cluster_desc = cluster->get_cluster_description()->get_harvesting_masks(chip_id);

        std::thread thread0([&]() {
            std::unique_ptr<ArcMessenger> arc_messenger = ArcMessenger::create_arc_messenger(tt_device);

            for (uint32_t loop = 0; loop < num_loops; loop++) {
                std::vector<uint32_t> arc_msg_return_values = {0};
                uint32_t response = arc_messenger->send_message(
                    wormhole::ARC_MSG_COMMON_PREFIX |
                        tt_device->get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
                    arc_msg_return_values,
                    {0, 0});

                EXPECT_EQ(
                    CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, arc_msg_return_values[0]),
                    harvesting_mask_cluster_desc.tensix_harvesting_mask);
            }
        });

        std::thread thread1([&]() {
            std::unique_ptr<ArcMessenger> arc_messenger = ArcMessenger::create_arc_messenger(tt_device);

            for (uint32_t loop = 0; loop < num_loops; loop++) {
                std::vector<uint32_t> arc_msg_return_values = {0};
                uint32_t response = arc_messenger->send_message(
                    wormhole::ARC_MSG_COMMON_PREFIX |
                        tt_device->get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
                    arc_msg_return_values,
                    {0, 0});

                EXPECT_EQ(
                    CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, arc_msg_return_values[0]),
                    harvesting_mask_cluster_desc.tensix_harvesting_mask);
            }
        });

        thread0.join();
        thread1.join();
    }
}
