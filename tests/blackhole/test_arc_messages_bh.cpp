// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_device/tt_device_factory.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/utils/timeouts.hpp"

using namespace tt::umd;

// The ArcMessenger these tests used to create built its own message queue, so it worked without
// init_tt_device(). The firmware command path refuses to talk to firmware that has not reported
// ready, so each test brings the firmware up first - init_firmware() is the lighter entry that
// skips the SocDescriptor construction the full init_tt_device() would add.

TEST(BlackholeArcMessages, BlackholeArcMessagesBasic) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_id);
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);

        const uint32_t num_loops = 100;
        for (int i = 0; i < num_loops; i++) {
            DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
                (uint32_t)blackhole::ArcMessageType::TEST, {}, timeout::ARC_MESSAGE_TIMEOUT);
            ASSERT_EQ(result.exit_code, 0u);
        }

        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    }
}

TEST(BlackholeArcMessages, BlackholeArcMessageArgPassing) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_id);
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);

        // TEST (0x90) increments the argument and returns it in word[1] of the response.
        unsigned int random_arg = 42;
        DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
            (uint32_t)blackhole::ArcMessageType::TEST, {random_arg}, timeout::ARC_MESSAGE_TIMEOUT);

        EXPECT_EQ(result.exit_code, 0u);
        ASSERT_FALSE(result.return_values.empty());
        EXPECT_EQ(result.return_values[0], random_arg + 1);

        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    }
}

TEST(BlackholeArcMessages, BlackholeArcMessageReturnValues) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_id);
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);

        DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
            (uint32_t)blackhole::ArcMessageType::READ_TS, {}, timeout::ARC_MESSAGE_TIMEOUT);

        EXPECT_EQ(result.exit_code, 0u);
        ASSERT_FALSE(result.return_values.empty());
        EXPECT_GT(result.return_values[0], 0u);

        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    }
}

TEST(BlackholeArcMessages, BlackholeArcMessageHigherAIClock) {
    const uint32_t ms_sleep = 2000;

    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_id);
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->init_tt_device();

        [[maybe_unused]] DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
            (uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY, {}, timeout::ARC_MESSAGE_TIMEOUT);

        // Wait for telemetry to update AICLK.
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));

        uint32_t aiclk = tt_device->get_clock();

        // TODO #781: For now expect only that busy val is something larger than idle val.
        EXPECT_GT(aiclk, blackhole::AICLK_IDLE_VAL);

        result = tt_device->get_device_firmware()->send_device_command(
            (uint32_t)blackhole::ArcMessageType::AICLK_GO_LONG_IDLE, {}, timeout::ARC_MESSAGE_TIMEOUT);

        // Wait for telemetry to update AICLK.
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));

        aiclk = tt_device->get_clock();

        EXPECT_EQ(aiclk, blackhole::AICLK_IDLE_VAL);

        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    }
}

TEST(BlackholeArcMessages, MultipleThreadsArcMessages) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    const uint32_t num_loops = 1000;

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_id);
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->init_tt_device();

        // Both threads drive the same firmware command path; the named ARC mutex serializes them
        // exactly as it serialized the per-thread ArcMessenger instances this test used to create.
        auto message_loop = [&]() {
            for (uint32_t loop = 0; loop < num_loops; loop++) {
                DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
                    (uint32_t)blackhole::ArcMessageType::TEST, {}, timeout::ARC_MESSAGE_TIMEOUT);
                ASSERT_EQ(result.exit_code, 0u);
            }
        };

        std::thread thread0(message_loop);
        std::thread thread1(message_loop);

        thread0.join();
        thread1.join();

        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    }
}
