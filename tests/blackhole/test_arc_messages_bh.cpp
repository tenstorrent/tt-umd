// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/arc/arc_messenger.hpp"
#include "tt-umd/arc/blackhole_arc_telemetry_reader.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/types/blackhole_arc.hpp"

using namespace tt::umd;

TEST(BlackholeArcMessages, BlackholeArcMessagesBasic) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(true);

        std::unique_ptr<ArcMessenger> bh_arc_messenger = ArcMessenger::create_arc_messenger(tt_device.get());

        const uint32_t num_loops = 100;
        for (int i = 0; i < num_loops; i++) {
            uint32_t response = bh_arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::TEST);
            ASSERT_EQ(response, 0);
        }

        tt_device->set_power_state(false);
    }
}

TEST(BlackholeArcMessages, BlackholeArcMessageArgPassing) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(true);

        std::unique_ptr<ArcMessenger> bh_arc_messenger = ArcMessenger::create_arc_messenger(tt_device.get());

        // TEST (0x90) increments the argument and returns it in word[1] of the response.
        unsigned int random_arg = 42;
        std::vector<uint32_t> return_values;
        uint32_t exit_code =
            bh_arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::TEST, return_values, {random_arg});

        EXPECT_EQ(exit_code, 0);
        ASSERT_FALSE(return_values.empty());
        EXPECT_EQ(return_values[0], random_arg + 1);

        tt_device->set_power_state(false);
    }
}

TEST(BlackholeArcMessages, BlackholeArcMessageReturnValues) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(true);

        std::unique_ptr<ArcMessenger> bh_arc_messenger = ArcMessenger::create_arc_messenger(tt_device.get());

        std::vector<uint32_t> return_values;
        uint32_t exit_code =
            bh_arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::READ_TS, return_values);

        EXPECT_EQ(exit_code, 0);
        ASSERT_FALSE(return_values.empty());
        EXPECT_GT(return_values[0], 0u);

        tt_device->set_power_state(false);
    }
}

TEST(BlackholeArcMessages, BlackholeArcMessageHigherAIClock) {
    const uint32_t ms_sleep = 2000;

    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(true);
        tt_device->init_tt_device();

        std::unique_ptr<ArcMessenger> bh_arc_messenger = ArcMessenger::create_arc_messenger(tt_device.get());

        [[maybe_unused]] uint32_t response =
            bh_arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY);

        // Wait for telemetry to update AICLK.
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));

        uint32_t aiclk = tt_device->get_clock();

        // TODO #781: For now expect only that busy val is something larger than idle val.
        EXPECT_GT(aiclk, blackhole::AICLK_IDLE_VAL);

        response = bh_arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::AICLK_GO_LONG_IDLE);

        // Wait for telemetry to update AICLK.
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));

        aiclk = tt_device->get_clock();

        EXPECT_EQ(aiclk, blackhole::AICLK_IDLE_VAL);

        tt_device->set_power_state(false);
    }
}

TEST(BlackholeArcMessages, MultipleThreadsArcMessages) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    const uint32_t num_loops = 1000;

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(true);
        tt_device->init_tt_device();

        std::thread thread0([&]() {
            std::unique_ptr<ArcMessenger> arc_messenger = ArcMessenger::create_arc_messenger(tt_device.get());

            for (uint32_t loop = 0; loop < num_loops; loop++) {
                uint32_t response = arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::TEST);
                ASSERT_EQ(response, 0);
            }
        });

        std::thread thread1([&]() {
            std::unique_ptr<ArcMessenger> arc_messenger = ArcMessenger::create_arc_messenger(tt_device.get());
            for (uint32_t loop = 0; loop < num_loops; loop++) {
                uint32_t response = arc_messenger->send_message((uint32_t)blackhole::ArcMessageType::TEST);
                ASSERT_EQ(response, 0);
            }
        });

        thread0.join();
        thread1.join();

        tt_device->set_power_state(false);
    }
}
