// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <numeric>

#include "assert.hpp"
#include "gtest/gtest.h"
#include "tt-logger/tt-logger.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/types/telemetry.hpp"

using namespace tt;
using namespace tt::umd;

class TestTelemetry : public ::testing::Test {
protected:
    static void SetDeviceType() {
        const char* jtag_env = std::getenv("TEST_USE_JTAG");
        if (jtag_env && std::string(jtag_env) == "1") {
            device_type_ = IODeviceType::JTAG;
        }
    }

    // This method runs once before ALL tests marked with TestTelemetry.
    static void SetUpTestSuite() {
        SetDeviceType();

        std::vector<int> device_ids;
        switch (device_type_) {
            case IODeviceType::JTAG: {
                auto device_cnt = JtagDevice::create()->get_device_cnt();
                device_ids = std::vector<int>(device_cnt);
                std::iota(device_ids.begin(), device_ids.end(), 0);
                break;
            }
            case IODeviceType::PCIe: {
                device_ids = PCIDevice::enumerate_devices();
                break;
            }
            default:
                TT_THROW("Unsupported device type");
        }

        for (int device_id : device_ids) {
            std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_id, device_type_);
            tt_device->init_tt_device();
            devices.push_back(std::move(tt_device));
        }

        setup_successful_ = true;
    }

    // This method runs before EACH individual test marked with TestTelemetry.
    void SetUp() override {
        // Check if devices were successfully set up in SetUpTestSuite
        if (!setup_successful_) {
            GTEST_SKIP() << "Skipping test as device setup was not successful.";
        }
    }

    static std::vector<std::unique_ptr<TTDevice>> devices;
    static IODeviceType device_type_;
    static bool setup_successful_;
};

/*static*/ std::vector<std::unique_ptr<TTDevice>> TestTelemetry::devices;
/*static*/ IODeviceType TestTelemetry::device_type_ = IODeviceType::PCIe;
/*static*/ bool TestTelemetry::setup_successful_ = false;

TEST_F(TestTelemetry, BasicTelemetry) {
    for (const auto& tt_device : devices) {
        if (tt_device->get_firmware_version() < semver_t(18, 4, 0)) {
            log_warning(
                tt::LogUMD,
                "Skipping telemetry test on device {} with firmware version {} < 18.4.0",
                tt_device->get_communication_device_id(),
                tt_device->get_firmware_version().to_string());
            continue;
        }

        ArcTelemetryReader* arc_telemetry_reader = tt_device->get_arc_telemetry_reader();

        uint32_t board_id_high = arc_telemetry_reader->read_entry(TelemetryTag::BOARD_ID_HIGH);
        uint32_t board_id_low = arc_telemetry_reader->read_entry(TelemetryTag::BOARD_ID_LOW);

        const uint64_t board_id = ((uint64_t)board_id_high << 32) | (board_id_low);
        EXPECT_NO_THROW(get_board_type_from_board_id(board_id));
    }
}

TEST_F(TestTelemetry, TelemetryEntryAvailable) {
    for (const auto& tt_device : devices) {
        tt_device->init_tt_device();
        ArcTelemetryReader* arc_telemetry_reader = tt_device->get_arc_telemetry_reader();

        EXPECT_TRUE(arc_telemetry_reader->is_entry_available(TelemetryTag::BOARD_ID_HIGH));
        EXPECT_TRUE(arc_telemetry_reader->is_entry_available(TelemetryTag::BOARD_ID_LOW));

        // Blackhole tag table is still not finalized, but we are probably never going to have 200 tags.
        EXPECT_FALSE(arc_telemetry_reader->is_entry_available(200));
    }
}
