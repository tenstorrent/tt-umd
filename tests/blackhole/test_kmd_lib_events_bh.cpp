// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/kmd_versions.hpp"
#include "tt-umd/utils/timeouts.hpp"

using namespace tt::umd;

// Firmware's TT_SMC_MSG_TEST_EVENT_RECORD publishes TEST records with fields the host chooses, so
// these tests can drive the whole firmware -> KMD -> tt-kmd-lib path without a real error source.

namespace {

constexpr uint32_t TEST_EVENT_RECORD_MSG = 0x91;
constexpr uint16_t EVENT_TYPE_TEST = 0x0000;
constexpr int READ_TIMEOUT_MS = 1000;

class KmdLibEventsBh : public ::testing::Test {
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

        // Opened before anything is published, since a handle sees only records that arrive after it was opened.
        const std::string chardev_path = "/dev/tenstorrent/" + std::to_string(pci_device_id);
        ASSERT_EQ(tt_device_open(chardev_path.c_str(), &dev, 0), 0);

        uint32_t count = 0;
        int fw_attached = 0;
        ASSERT_EQ(tt_device_read_events(dev, nullptr, 0, 0, 0, -1, &count, nullptr, &fw_attached), 0);
        if (!fw_attached) {
            GTEST_SKIP() << "Firmware does not publish device events.";
        }

        tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->get_device_firmware()->init_firmware(timeout::ARC_STARTUP_TIMEOUT);
    }

    void TearDown() override {
        if (tt_device) {
            tt_device->set_power_state(TTDevice::PowerState::IDLE);
        }
        if (dev) {
            tt_device_close(dev);
        }
    }

    // Firmware stamps record i with data[2] = data2 + i.
    void publish_test_events(
        uint16_t source, uint16_t repeat, uint32_t cause, uint32_t data0, uint32_t data1, uint32_t data2) {
        DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
            TEST_EVENT_RECORD_MSG,
            {static_cast<uint32_t>(source) | (static_cast<uint32_t>(repeat) << 16), cause, data0, data1, data2},
            timeout::ARC_MESSAGE_TIMEOUT);
        ASSERT_EQ(result.exit_code, 0u);
    }

    // The driver may hand a batch over in several reads, so keep reading until `expected` records arrive or a read
    // times out.
    std::vector<tt_event_t> read_events(uint32_t expected) {
        std::vector<tt_event_t> events(expected);
        uint32_t total = 0;
        while (total < expected) {
            uint32_t count = 0;
            EXPECT_EQ(
                tt_device_read_events(
                    dev, events.data() + total, expected - total, 0, READ_TIMEOUT_MS, -1, &count, nullptr, nullptr),
                0);
            if (count == 0) {
                break;
            }
            total += count;
        }
        events.resize(total);
        return events;
    }

    tt_device_t* dev = nullptr;
    std::unique_ptr<TTDevice> tt_device;
};

}  // namespace

TEST_F(KmdLibEventsBh, RecordRoundTripsEveryField) {
    publish_test_events(0x0042, 1, 7, 1, 2, 3);

    std::vector<tt_event_t> events = read_events(1);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EVENT_TYPE_TEST);
    EXPECT_EQ(events[0].source, 0x0042);
    EXPECT_EQ(events[0].cause, 7u);
    EXPECT_EQ(events[0].data[0], 1u);
    EXPECT_EQ(events[0].data[1], 2u);
    EXPECT_EQ(events[0].data[2], 3u);
    EXPECT_EQ(events[0].data[3], 0u);
}

TEST_F(KmdLibEventsBh, BatchArrivesInOrder) {
    publish_test_events(0, 3, 0, 0, 0, 10);

    std::vector<tt_event_t> events = read_events(3);

    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].data[2], 10u);
    EXPECT_EQ(events[1].data[2], 11u);
    EXPECT_EQ(events[2].data[2], 12u);
}

TEST_F(KmdLibEventsBh, WaitReturnsRecordPublishedDuringIt) {
    std::future<std::vector<tt_event_t>> reader = std::async(std::launch::async, [this] { return read_events(1); });

    publish_test_events(0, 1, 0, 0, 0, 5);

    std::vector<tt_event_t> events = reader.get();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data[2], 5u);
}

TEST_F(KmdLibEventsBh, WaitTimesOutWithoutRecords) {
    tt_event_t event = {};
    uint32_t count = 1;

    EXPECT_EQ(tt_device_read_events(dev, &event, 1, 0, 100, -1, &count, nullptr, nullptr), 0);
    EXPECT_EQ(count, 0u);
}

TEST_F(KmdLibEventsBh, ReadableWakeFdEndsTheWait) {
    const int wake_fd = eventfd(1, EFD_CLOEXEC);
    ASSERT_GE(wake_fd, 0);
    tt_event_t event = {};
    uint32_t count = 0;

    EXPECT_EQ(tt_device_read_events(dev, &event, 1, 0, 30000, wake_fd, &count, nullptr, nullptr), -ECANCELED);

    close(wake_fd);
}
