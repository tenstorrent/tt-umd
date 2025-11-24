// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

using namespace tt::umd;

TEST(WormholeTelemetry, BasicWormholeTelemetry) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        std::unique_ptr<ArcTelemetryReader> blackhole_arc_telemetry_reader =
            ArcTelemetryReader::create_arc_telemetry_reader(tt_device.get());

        uint32_t board_id_high = blackhole_arc_telemetry_reader->read_entry(wormhole::TelemetryTag::BOARD_ID_HIGH);
        uint32_t board_id_low = blackhole_arc_telemetry_reader->read_entry(wormhole::TelemetryTag::BOARD_ID_LOW);

        const uint64_t board_id = ((uint64_t)board_id_high << 32) | (board_id_low);
        EXPECT_NO_THROW(get_board_type_from_board_id(board_id));
    }
}

TEST(WormholeTelemetry, WormholeTelemetryEntryAvailable) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        std::unique_ptr<ArcTelemetryReader> telemetry =
            ArcTelemetryReader::create_arc_telemetry_reader(tt_device.get());

        for (uint32_t telem_tag = 0; telem_tag < wormhole::TelemetryTag::NUMBER_OF_TAGS; telem_tag++) {
            EXPECT_TRUE(telemetry->is_entry_available(telem_tag));
        }

        EXPECT_FALSE(telemetry->is_entry_available(wormhole::TelemetryTag::NUMBER_OF_TAGS));
    }
}

TEST(TestTelemetry, CompareTwoTelemetryValues) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();
        ArcTelemetryReader* arc_telemetry_reader = tt_device->get_arc_telemetry_reader();

        std::unique_ptr<SmBusArcTelemetryReader> smbus_telemetry_reader =
            std::make_unique<SmBusArcTelemetryReader>(tt_device.get());

        EXPECT_EQ(
            arc_telemetry_reader->read_entry(TelemetryTag::DM_BL_FW_VERSION),
            smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::DM_BL_FW_VERSION));

        EXPECT_EQ(
            arc_telemetry_reader->read_entry(TelemetryTag::DM_APP_FW_VERSION),
            smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::DM_APP_FW_VERSION));

        EXPECT_EQ(
            arc_telemetry_reader->read_entry(TelemetryTag::TT_FLASH_VERSION),
            smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::TT_FLASH_VERSION));

        EXPECT_EQ(
            arc_telemetry_reader->read_entry(TelemetryTag::ETH_FW_VERSION),
            smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::ETH_FW_VERSION));
    }
}
