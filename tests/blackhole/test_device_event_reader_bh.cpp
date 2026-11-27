// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "tests/test_utils/device_event_test_utils.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/device_event_reader.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/kmd_versions.hpp"
#include "tt-umd/utils/timeouts.hpp"

using namespace tt::umd;
using test_utils::CANCELLED_READ_BOUND;
using test_utils::CANCELLED_READ_TIMEOUT;
using test_utils::EVENT_READ_TIMEOUT;
using test_utils::EVENT_TYPE_TEST;
using test_utils::TEST_EVENT_RECORD_MSG;

// The tests publish TEST records through firmware.

// Not in an anonymous namespace: in a unity build, GCC rejects a TEST_F class deriving from one.
class DeviceEventReaderBh : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent devices found.";
        }
        const int pci_device_id = pci_device_ids.front();

        if (!PCIDevice::is_device_events_supported()) {
            GTEST_SKIP() << "KMD " << PCIDevice::read_kmd_version().str() << " is below required "
                         << KMD_READ_EVENTS.str();
        }

        tt_device = TTDevice::create(pci_device_id);

        // Created before anything is published, since a reader sees only records that arrive after it.
        reader = tt_device->create_event_reader();
        ASSERT_NE(reader, nullptr);
        if (!reader->is_firmware_attached()) {
            GTEST_SKIP() << "Firmware does not publish device events.";
        }

        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);
    }

    void TearDown() override {
        if (tt_device) {
            tt_device->set_power_state(TTDevice::PowerState::IDLE);
        }
    }

    void publish_test_event(uint16_t source, uint32_t cause, uint32_t data0, uint32_t data1, uint32_t data2) {
        constexpr uint32_t repeat = 1;
        DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
            TEST_EVENT_RECORD_MSG,
            {static_cast<uint32_t>(source) | repeat << 16, cause, data0, data1, data2},
            timeout::ARC_MESSAGE_TIMEOUT);
        ASSERT_EQ(result.exit_code, 0u);
    }

    std::unique_ptr<TTDevice> tt_device;
    std::unique_ptr<DeviceEventReader> reader;
};

TEST_F(DeviceEventReaderBh, ReadReturnsPublishedRecord) {
    publish_test_event(0x0042, 7, 1, 2, 3);

    DeviceEventBatch batch = reader->read_for(4, EVENT_READ_TIMEOUT);

    ASSERT_EQ(batch.events.size(), 1u);
    EXPECT_EQ(batch.events[0].type, EVENT_TYPE_TEST);
    EXPECT_EQ(batch.events[0].source, 0x0042);
    EXPECT_EQ(batch.events[0].cause, 7u);
    EXPECT_EQ(batch.events[0].data, (std::array<uint32_t, 4>{1, 2, 3, 0}));
    EXPECT_EQ(batch.lost, 0u);
    EXPECT_FALSE(batch.device_lost);
}

TEST_F(DeviceEventReaderBh, ReadForTimesOutWithoutRecords) {
    DeviceEventBatch batch = reader->read_for(4, std::chrono::milliseconds(100));

    EXPECT_TRUE(batch.events.empty());
    EXPECT_FALSE(batch.device_lost);
}

class DeviceEventReaderNoWaitBh : public DeviceEventReaderBh,
                                  public ::testing::WithParamInterface<std::chrono::milliseconds> {};

TEST_P(DeviceEventReaderNoWaitBh, ReadForDoesNotWaitWithoutRecords) {
    const auto start = std::chrono::steady_clock::now();
    DeviceEventBatch batch = reader->read_for(4, GetParam());
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(batch.events.empty());
    EXPECT_LT(elapsed, CANCELLED_READ_BOUND);
}

INSTANTIATE_TEST_SUITE_P(
    ZeroAndNegativeTimeouts,
    DeviceEventReaderNoWaitBh,
    ::testing::Values(std::chrono::milliseconds(0), std::chrono::milliseconds(-30000)));

TEST_F(DeviceEventReaderBh, ReadWaitsForRecordPublishedDuringIt) {
    std::promise<void> read_done;
    std::future<void> publisher = std::async(std::launch::async, [this, read_finished = read_done.get_future()] {
        // Published only after the read has had time to start waiting.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        publish_test_event(0, 0, 0, 0, 5);
        // A record that never arrives, including one whose publish failed, ends the read empty and fails the
        // size check below, instead of hanging the test binary.
        if (read_finished.wait_for(EVENT_READ_TIMEOUT) == std::future_status::timeout) {
            reader->cancel();
        }
    });

    DeviceEventBatch batch = reader->read(4);
    read_done.set_value();
    publisher.get();

    ASSERT_EQ(batch.events.size(), 1u);
    EXPECT_EQ(batch.events[0].data[2], 5u);
}

TEST_F(DeviceEventReaderBh, ReadersDoNotTakeRecordsFromEachOther) {
    std::unique_ptr<DeviceEventReader> other_reader = tt_device->create_event_reader();

    publish_test_event(0, 0, 0, 0, 9);

    DeviceEventBatch batch = reader->read_for(4, EVENT_READ_TIMEOUT);
    DeviceEventBatch other_batch = other_reader->read_for(4, EVENT_READ_TIMEOUT);
    ASSERT_EQ(batch.events.size(), 1u);
    ASSERT_EQ(other_batch.events.size(), 1u);
    EXPECT_EQ(batch.events[0].data[2], 9u);
    EXPECT_EQ(other_batch.events[0].data[2], 9u);
}

TEST_F(DeviceEventReaderBh, CancelEndsWaitInProgress) {
    std::future<void> canceller = std::async(std::launch::async, [this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        reader->cancel();
    });

    const auto start = std::chrono::steady_clock::now();
    DeviceEventBatch batch = reader->read_for(4, CANCELLED_READ_TIMEOUT);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.get();

    EXPECT_TRUE(batch.events.empty());
    EXPECT_LT(elapsed, CANCELLED_READ_BOUND);
}

TEST_F(DeviceEventReaderBh, CancelBeforeReadEndsTheNextWait) {
    reader->cancel();

    const auto start = std::chrono::steady_clock::now();
    DeviceEventBatch batch = reader->read_for(4, CANCELLED_READ_TIMEOUT);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(batch.events.empty());
    EXPECT_LT(elapsed, CANCELLED_READ_BOUND);
}

TEST_F(DeviceEventReaderBh, CancelEndsOnlyOneWait) {
    reader->cancel();
    reader->cancel();
    reader->read_for(4, CANCELLED_READ_TIMEOUT);

    const auto start = std::chrono::steady_clock::now();
    reader->read_for(4, std::chrono::milliseconds(200));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(200));
}
