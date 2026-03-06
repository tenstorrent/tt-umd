// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/types/telemetry.hpp"

using namespace tt;
using namespace tt::umd;

TEST(TestTelemetry, BasicTelemetry) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();
        if (tt_device->get_firmware_version() < FirmwareBundleVersion(18, 4, 0)) {
            log_warning(
                tt::LogUMD,
                "Skipping telemetry test on device {} with firmware version {} < 18.4.0",
                pci_device_id,
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

TEST(TestTelemetry, TelemetryEntryAvailable) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();
        ArcTelemetryReader* arc_telemetry_reader = tt_device->get_arc_telemetry_reader();

        EXPECT_TRUE(arc_telemetry_reader->is_entry_available(TelemetryTag::BOARD_ID_HIGH));
        EXPECT_TRUE(arc_telemetry_reader->is_entry_available(TelemetryTag::BOARD_ID_LOW));

        // Blackhole tag table is still not finalized, but we are probably never going to have 200 tags.
        EXPECT_FALSE(arc_telemetry_reader->is_entry_available(200));
    }
}

TEST(TestTelemetry, RemoteTelemetry) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();
    auto remote_chips = umd_cluster->get_target_remote_device_ids();
    if (remote_chips.empty()) {
        GTEST_SKIP() << "No remote devices found in cluster.";
    }
    auto remote_chip = umd_cluster->get_remote_chip(*remote_chips.begin());
    TTDevice* remote_device = remote_chip->get_tt_device();
    TTDevice* local_device = remote_chip->get_remote_communication()->get_local_device();
    ArcTelemetryReader* remote_telemetry = remote_device->get_arc_telemetry_reader();
    ArcTelemetryReader* local_telemetry = local_device->get_arc_telemetry_reader();

    EXPECT_TRUE(remote_telemetry->is_entry_available(TelemetryTag::BOARD_ID_LOW));
    EXPECT_TRUE(remote_telemetry->is_entry_available(TelemetryTag::BOARD_ID_HIGH));
    EXPECT_TRUE(remote_telemetry->is_entry_available(TelemetryTag::ASIC_LOCATION));
    EXPECT_TRUE(
        remote_telemetry->read_entry(TelemetryTag::BOARD_ID_HIGH) ==
        local_telemetry->read_entry(TelemetryTag::BOARD_ID_HIGH));
    EXPECT_TRUE(
        remote_telemetry->read_entry(TelemetryTag::BOARD_ID_LOW) ==
        local_telemetry->read_entry(TelemetryTag::BOARD_ID_LOW));
    EXPECT_FALSE(
        remote_telemetry->read_entry(TelemetryTag::ASIC_LOCATION) ==
        local_telemetry->read_entry(TelemetryTag::ASIC_LOCATION));
}
