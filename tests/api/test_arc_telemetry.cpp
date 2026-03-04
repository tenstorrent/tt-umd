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
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/types/gddr_telemetry.hpp"
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

TEST(TestTelemetry, GddrTelemetry) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    ARCH arch = ARCH::Invalid;
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test..";
    }

    // Scope ensures PCIDevice is destroyed after reading arch, avoiding holding mappings for the test duration.
    {
        PCIDevice pci_device(pci_device_ids.at(0));
        arch = pci_device.get_arch();
    }

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        auto fw_info = FirmwareInfoProvider::create_firmware_info_provider(tt_device.get());

        log_info(tt::LogUMD, "Testing GDDR Telemetry with PCI ID {}.", pci_device_id);

        auto dram_speed = fw_info->get_dram_speed();

        // DRAM speed telemetry on Wormhole is available starting from firmware 18.4.0.
        if (arch == ARCH::WORMHOLE_B0 && tt_device->get_firmware_version() < FirmwareBundleVersion(18, 4, 0)) {
            EXPECT_FALSE(dram_speed.has_value()) << "GDDR speed should not be available for Wormhole firmware version "
                                                 << tt_device->get_firmware_version().to_string() << " < 18.4.0";
            log_info(tt::LogUMD, "GDDR speed not available for Wormhole firmware < 18.4.0.");
            continue;
        }

        // For Wormhole with firmware >= 18.4.0 and all Blackhole firmware DRAM speed should be available.
        EXPECT_TRUE(dram_speed.has_value()) << "GDDR speed should be available.";
        if (dram_speed.has_value()) {
            log_info(tt::LogUMD, "GDDR speed: {} Mbps", dram_speed.value());
        }

        if (arch == ARCH::WORMHOLE_B0) {
            log_debug(
                tt::LogUMD,
                "Only GDDR speed and status are populated on Wormhole and only speed is verified in this test.");
            continue;
        }

        auto gddr_telemetry = fw_info->get_aggregated_dram_telemetry();
        ASSERT_TRUE(gddr_telemetry.has_value()) << "GDDR telemetry should be available on Blackhole.";

        // Max temperature is fetched from the same telemetry source (all GDDR module temperatures).
        auto max_temp = fw_info->get_current_max_dram_temperature();
        if (max_temp.has_value()) {
            log_info(tt::LogUMD, "Max GDDR temperature from all modules: {}C", max_temp.value());
        }

        log_info(tt::LogUMD, "Per-module GDDR telemetry:");
        for (const auto& [gddr_index, module_telemetry] : gddr_telemetry->modules) {
            log_info(
                tt::LogUMD,
                "GDDR_{}: top={}C bottom={}C corr_rd={} corr_wr={} uncorr_rd={} uncorr_wr={}",
                static_cast<int>(gddr_index),
                module_telemetry.dram_temperature_top,
                module_telemetry.dram_temperature_bottom,
                module_telemetry.corr_edc_rd_errors,
                module_telemetry.corr_edc_wr_errors,
                module_telemetry.uncorr_edc_rd_error,
                module_telemetry.uncorr_edc_wr_error);
        }

        uint16_t max_temp_from_modules = 0;
        for (const auto& [gddr_index, module_telemetry] : gddr_telemetry->modules) {
            max_temp_from_modules = std::max(max_temp_from_modules, module_telemetry.dram_temperature_top);
            max_temp_from_modules = std::max(max_temp_from_modules, module_telemetry.dram_temperature_bottom);
        }

        EXPECT_EQ(max_temp.value(), max_temp_from_modules)
            << "Max temperature should match the maximum from all module temperatures.";

        // Test individual module telemetry access.
        log_info(tt::LogUMD, "Testing individual module access:");
        for (auto gddr_index :
             {BlackholeGddr::GDDR_7,
              BlackholeGddr::GDDR_6,
              BlackholeGddr::GDDR_5,
              BlackholeGddr::GDDR_4,
              BlackholeGddr::GDDR_3,
              BlackholeGddr::GDDR_2,
              BlackholeGddr::GDDR_1,
              BlackholeGddr::GDDR_0}) {
            auto module_telemetry = fw_info->get_dram_telemetry(gddr_index);
            ASSERT_TRUE(module_telemetry.has_value()) << "Individual GDDR module telemetry should be available.";

            log_info(
                tt::LogUMD,
                "GDDR_{}: top={}C bottom={}C",
                static_cast<int>(gddr_index),
                module_telemetry->dram_temperature_top,
                module_telemetry->dram_temperature_bottom);

            // Verify that individual access matches aggregated data.
            EXPECT_EQ(
                module_telemetry->dram_temperature_top, gddr_telemetry->modules.at(gddr_index).dram_temperature_top);
            EXPECT_EQ(
                module_telemetry->dram_temperature_bottom,
                gddr_telemetry->modules.at(gddr_index).dram_temperature_bottom);
        }
    }
}
