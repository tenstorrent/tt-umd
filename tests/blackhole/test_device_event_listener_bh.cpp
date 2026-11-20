// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/device_event_listener.hpp"
#include "tt-umd/tt_device/device_event_reader.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/kmd_versions.hpp"
#include "tt-umd/utils/timeouts.hpp"

using namespace tt::umd;

// Firmware's TT_SMC_MSG_TEST_EVENT_RECORD publishes TEST records with fields the host chooses.

namespace {

constexpr uint32_t TEST_EVENT_RECORD_MSG = 0x91;
constexpr uint16_t EVENT_TYPE_TEST = 0x0000;
constexpr uint64_t KEY = 42;
constexpr auto READ_TIMEOUT = std::chrono::milliseconds(1000);
// Long enough that a read returning well within it was ended by cancel(), not by the timeout.
constexpr auto CANCELLED_READ_TIMEOUT = std::chrono::seconds(30);
constexpr auto CANCELLED_READ_BOUND = std::chrono::seconds(10);

std::unique_ptr<TTDevice> create_device(int pci_device_id) {
    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
    tt_device->set_power_state(TTDevice::PowerState::BUSY);
    tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);
    return tt_device;
}

void publish_test_event(TTDevice& tt_device, uint32_t data2) {
    constexpr uint32_t repeat = 1;
    DeviceCommandResult result = tt_device.get_device_firmware()->send_device_command(
        TEST_EVENT_RECORD_MSG, {repeat << 16, 0, 0, 0, data2}, timeout::ARC_MESSAGE_TIMEOUT);
    ASSERT_EQ(result.exit_code, 0u);
}

class DeviceEventListenerBh : public ::testing::Test {
protected:
    void SetUp() override {
        pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent devices found.";
        }

        if (!PCIDevice::is_device_events_supported()) {
            GTEST_SKIP() << "KMD " << PCIDevice::read_kmd_version().str() << " is below required "
                         << KMD_READ_EVENTS.str();
        }

        tt_device = TTDevice::create(pci_device_ids.front());
        std::unique_ptr<DeviceEventReader> probe = tt_device->create_event_reader();
        ASSERT_NE(probe, nullptr);
        if (!probe->is_firmware_attached()) {
            GTEST_SKIP() << "Firmware does not publish device events.";
        }

        // Created before anything is published, since a listener sees only records that arrive after it.
        std::set<uint64_t> uncovered_keys;
        listener = DeviceEventListener::create({{KEY, tt_device.get()}}, &uncovered_keys);
        ASSERT_TRUE(uncovered_keys.empty());

        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);
    }

    void TearDown() override {
        if (tt_device) {
            tt_device->set_power_state(TTDevice::PowerState::IDLE);
        }
    }

    std::vector<int> pci_device_ids;
    std::unique_ptr<TTDevice> tt_device;
    std::unique_ptr<DeviceEventListener> listener;
};

}  // namespace

TEST_F(DeviceEventListenerBh, ReadReturnsPublishedRecordUnderKey) {
    publish_test_event(*tt_device, 3);

    std::vector<KeyedDeviceEventBatch> batches = listener->read_for(4, READ_TIMEOUT);

    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].key, KEY);
    ASSERT_EQ(batches[0].batch.events.size(), 1u);
    EXPECT_EQ(batches[0].batch.events[0].type, EVENT_TYPE_TEST);
    EXPECT_EQ(batches[0].batch.events[0].data[2], 3u);
    EXPECT_EQ(batches[0].batch.lost, 0u);
    EXPECT_FALSE(batches[0].batch.device_lost);
}

TEST_F(DeviceEventListenerBh, ReadForTimesOutWithoutRecords) {
    EXPECT_TRUE(listener->read_for(4, std::chrono::milliseconds(100)).empty());
}

TEST_F(DeviceEventListenerBh, ReadWaitsForRecordPublishedDuringIt) {
    // Published only after the read has had time to start waiting.
    std::future<void> publisher = std::async(std::launch::async, [this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        publish_test_event(*tt_device, 5);
    });

    std::vector<KeyedDeviceEventBatch> batches = listener->read(4);
    publisher.get();

    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].batch.events.size(), 1u);
    EXPECT_EQ(batches[0].batch.events[0].data[2], 5u);
}

TEST_F(DeviceEventListenerBh, DeviceUnderTwoKeysIsReportedUnderEach) {
    constexpr uint64_t other_key = 7;
    std::set<uint64_t> uncovered_keys;
    listener = DeviceEventListener::create({{KEY, tt_device.get()}, {other_key, tt_device.get()}}, &uncovered_keys);
    ASSERT_TRUE(uncovered_keys.empty());

    publish_test_event(*tt_device, 9);

    // The two handles may become ready in separate waits.
    std::vector<uint64_t> keys;
    const auto deadline = std::chrono::steady_clock::now() + READ_TIMEOUT;
    while (keys.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        for (const KeyedDeviceEventBatch& keyed : listener->read_for(4, READ_TIMEOUT)) {
            ASSERT_EQ(keyed.batch.events.size(), 1u);
            EXPECT_EQ(keyed.batch.events[0].data[2], 9u);
            keys.push_back(keyed.key);
        }
    }

    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys, (std::vector<uint64_t>{other_key, KEY}));
}

TEST_F(DeviceEventListenerBh, ListenerAndReaderDoNotTakeRecordsFromEachOther) {
    std::unique_ptr<DeviceEventReader> reader = tt_device->create_event_reader();

    publish_test_event(*tt_device, 11);

    std::vector<KeyedDeviceEventBatch> batches = listener->read_for(4, READ_TIMEOUT);
    DeviceEventBatch reader_batch = reader->read_for(4, READ_TIMEOUT);
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].batch.events.size(), 1u);
    ASSERT_EQ(reader_batch.events.size(), 1u);
    EXPECT_EQ(batches[0].batch.events[0].data[2], 11u);
    EXPECT_EQ(reader_batch.events[0].data[2], 11u);
}

TEST_F(DeviceEventListenerBh, RecordIsReportedUnderItsDevicesKey) {
    if (pci_device_ids.size() < 2) {
        GTEST_SKIP() << "Needs two Tenstorrent devices.";
    }

    constexpr uint64_t second_key = 1;
    std::unique_ptr<TTDevice> second_device = create_device(pci_device_ids[1]);
    std::set<uint64_t> uncovered_keys;
    listener =
        DeviceEventListener::create({{KEY, tt_device.get()}, {second_key, second_device.get()}}, &uncovered_keys);
    ASSERT_TRUE(uncovered_keys.empty());

    publish_test_event(*second_device, 13);
    std::vector<KeyedDeviceEventBatch> batches = listener->read_for(4, READ_TIMEOUT);
    second_device->set_power_state(TTDevice::PowerState::IDLE);

    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].key, second_key);
    ASSERT_EQ(batches[0].batch.events.size(), 1u);
    EXPECT_EQ(batches[0].batch.events[0].data[2], 13u);
}

TEST_F(DeviceEventListenerBh, CancelEndsWaitInProgress) {
    std::future<void> canceller = std::async(std::launch::async, [this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        listener->cancel();
    });

    const auto start = std::chrono::steady_clock::now();
    std::vector<KeyedDeviceEventBatch> batches = listener->read_for(4, CANCELLED_READ_TIMEOUT);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.get();

    EXPECT_TRUE(batches.empty());
    EXPECT_LT(elapsed, CANCELLED_READ_BOUND);
}

TEST_F(DeviceEventListenerBh, CancelEndsOnlyOneWait) {
    listener->cancel();
    listener->cancel();
    listener->read_for(4, CANCELLED_READ_TIMEOUT);

    const auto start = std::chrono::steady_clock::now();
    listener->read_for(4, std::chrono::milliseconds(200));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(200));
}
