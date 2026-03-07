// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/types/telemetry.hpp"

using namespace tt;
using namespace tt::umd;

// All defined TelemetryTag values (excluding NUMBER_OF_TAGS which is a sentinel).
static constexpr std::array<TelemetryTag, 47> ALL_TELEMETRY_TAGS = {{
    TelemetryTag::BOARD_ID_HIGH,
    TelemetryTag::BOARD_ID_LOW,
    TelemetryTag::ASIC_ID,
    TelemetryTag::HARVESTING_STATE,
    TelemetryTag::UPDATE_TELEM_SPEED,
    TelemetryTag::VCORE,
    TelemetryTag::TDP,
    TelemetryTag::TDC,
    TelemetryTag::VDD_LIMITS,
    TelemetryTag::THM_LIMITS,
    TelemetryTag::ASIC_TEMPERATURE,
    TelemetryTag::VREG_TEMPERATURE,
    TelemetryTag::BOARD_TEMPERATURE,
    TelemetryTag::AICLK,
    TelemetryTag::AXICLK,
    TelemetryTag::ARCCLK,
    TelemetryTag::L2CPUCLK0,
    TelemetryTag::L2CPUCLK1,
    TelemetryTag::L2CPUCLK2,
    TelemetryTag::L2CPUCLK3,
    TelemetryTag::ETH_LIVE_STATUS,
    TelemetryTag::DDR_STATUS,
    TelemetryTag::DDR_SPEED,
    TelemetryTag::ETH_FW_VERSION,
    TelemetryTag::GDDR_FW_VERSION,
    TelemetryTag::DM_APP_FW_VERSION,
    TelemetryTag::DM_BL_FW_VERSION,
    TelemetryTag::FLASH_BUNDLE_VERSION,
    TelemetryTag::CM_FW_VERSION,
    TelemetryTag::L2CPU_FW_VERSION,
    TelemetryTag::FAN_SPEED,
    TelemetryTag::TIMER_HEARTBEAT,
    TelemetryTag::TELEMETRY_ENUM_COUNT,
    TelemetryTag::ENABLED_TENSIX_COL,
    TelemetryTag::ENABLED_ETH,
    TelemetryTag::ENABLED_GDDR,
    TelemetryTag::ENABLED_L2CPU,
    TelemetryTag::PCIE_USAGE,
    TelemetryTag::NOC_TRANSLATION,
    TelemetryTag::FAN_RPM,
    TelemetryTag::ASIC_LOCATION,
    TelemetryTag::TDC_LIMIT_MAX,
    TelemetryTag::TT_FLASH_VERSION,
    TelemetryTag::ASIC_ID_HIGH,
    TelemetryTag::ASIC_ID_LOW,
    TelemetryTag::AICLK_LIMIT_MAX,
    TelemetryTag::TDP_LIMIT_MAX,
}};

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

TEST(TestTelemetry, BulkReadMatchesIndividualReads) {
    for (int id : PCIDevice::enumerate_devices()) {
        auto tt_device = TTDevice::create(id);
        tt_device->init_tt_device();
        ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();

        const auto& bulk = telemetry->read_all_entries();
        ASSERT_FALSE(bulk.empty());

        // Every tag present in the bulk read must match a single read_entry call.
        for (const auto& [tag, bulk_value] : bulk) {
            ASSERT_TRUE(telemetry->is_entry_available(static_cast<uint8_t>(tag)));
            EXPECT_EQ(bulk_value, telemetry->read_entry(static_cast<uint8_t>(tag)))
                << "Mismatch for telemetry tag " << tag;
        }
    }
}

TEST(TestTelemetry, BulkReadVsIndividualReadPerformance) {
    for (int id : PCIDevice::enumerate_devices()) {
        auto tt_device = TTDevice::create(id);
        tt_device->init_tt_device();
        ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();

        static constexpr int iterations = 100;

        // Individual reads: one read_from_device per tag per iteration.
        auto t0 = std::chrono::high_resolution_clock::now();

        // Filter to only tags available on this device.
        std::vector<TelemetryTag> available_tags;
        for (TelemetryTag tag : ALL_TELEMETRY_TAGS) {
            if (telemetry->is_entry_available(static_cast<uint8_t>(tag))) {
                available_tags.push_back(tag);
            }
        }
        ASSERT_FALSE(available_tags.empty());

        for (int i = 0; i < iterations; ++i) {
            for (TelemetryTag tag : available_tags) {
                volatile uint32_t val = telemetry->read_entry(static_cast<uint8_t>(tag));
                (void)val;
            }
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        // Bulk read: one read_from_device for the whole table per iteration.
        auto t2 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            const auto& bulk = telemetry->read_all_entries();
            (void)bulk;
        }
        auto t3 = std::chrono::high_resolution_clock::now();

        auto individual_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        auto bulk_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

        log_info(
            tt::LogUMD,
            "Device {}: {} tags, {} iterations — individual: {} us, bulk: {} us, speedup: {:.2f}x",
            id,
            available_tags.size(),
            iterations,
            individual_us,
            bulk_us,
            bulk_us > 0 ? static_cast<double>(individual_us) / static_cast<double>(bulk_us) : 0.0);

        // Bulk read should not be slower than individual reads.
        EXPECT_LE(bulk_us, individual_us);
    }
}
