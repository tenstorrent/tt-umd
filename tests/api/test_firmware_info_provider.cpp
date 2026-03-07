// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/utils/semver.hpp"

using namespace tt;
using namespace tt::umd;

static std::string dram_training_status_to_str(DramTrainingStatus status) {
    switch (status) {
        case DramTrainingStatus::IN_PROGRESS:
            return "IN_PROGRESS";
        case DramTrainingStatus::FAIL:
            return "FAIL";
        case DramTrainingStatus::SUCCESS:
            return "SUCCESS";
        default:
            return "UNKNOWN";
    }
}

static std::string fw_range_label(const FirmwareBundleVersion& fw_version) {
    if (fw_version <= FirmwareBundleVersion(18, 3, 0)) {
        return "LEGACY (<= 18.3)";
    } else if (fw_version <= FirmwareBundleVersion(18, 7, 0)) {
        return "TRANSITIONAL (18.4 - 18.7)";
    } else {
        return "MODERN (> 18.7)";
    }
}

// Helper to get the number of DRAM channels for the given architecture.
static uint32_t get_num_dram_channels(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return 6;
        case tt::ARCH::BLACKHOLE:
            return 8;
        default:
            return 0;
    }
}

// Helper to get the maximum clock frequency (AICLK_BUSY_VAL) for the given architecture.
static uint32_t get_aiclk_busy_val(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return wormhole::AICLK_BUSY_VAL;
        case tt::ARCH::BLACKHOLE:
            return blackhole::AICLK_BUSY_VAL;
        default:
            return 0;
    }
}

TEST(TestFirmwareInfoProvider, StaticVersionInfo) {  // Baremetal test
    // Test static methods that don't need a device.
    FirmwareBundleVersion wh_min = FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH::WORMHOLE_B0);
    FirmwareBundleVersion bh_min = FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH::BLACKHOLE);

    log_info(tt::LogUMD, "WH min compatible FW: {}", wh_min.to_string());
    log_info(tt::LogUMD, "BH min compatible FW: {}", bh_min.to_string());

    // Wormhole supports all firmware versions (no minimum).
    EXPECT_EQ(wh_min, FirmwareBundleVersion(0, 0, 0));

    // Blackhole requires at least 18.5.0.
    EXPECT_EQ(bh_min, FirmwareBundleVersion(18, 5, 0));

    FirmwareBundleVersion wh_latest =
        FirmwareInfoProvider::get_latest_supported_firmware_version(tt::ARCH::WORMHOLE_B0);
    FirmwareBundleVersion bh_latest = FirmwareInfoProvider::get_latest_supported_firmware_version(tt::ARCH::BLACKHOLE);

    log_info(tt::LogUMD, "WH latest supported FW: {}", wh_latest.to_string());
    log_info(tt::LogUMD, "BH latest supported FW: {}", bh_latest.to_string());

    // Both architectures report the same latest supported version currently.
    EXPECT_EQ(wh_latest, bh_latest);
}

TEST(TestFirmwareInfoProvider, BoardId) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        uint64_t board_id = fw_info->get_board_id();
        log_info(
            tt::LogUMD,
            "Device {}: board_id=0x{:016x}, fw_range={}",
            pci_device_id,
            board_id,
            fw_range_label(fw_info->get_firmware_version()));

        EXPECT_NE(board_id, 0);

        // Board ID should map to a known board type.
        EXPECT_NO_THROW(get_board_type_from_board_id(board_id));

        BoardType board_type = get_board_type_from_board_id(board_id);
        log_info(tt::LogUMD, "board_type={}", board_type_to_string(board_type));
    }
}

TEST(TestFirmwareInfoProvider, Temperature) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        double asic_temp = fw_info->get_asic_temperature();
        std::optional<double> board_temp = fw_info->get_board_temperature();

        log_info(
            tt::LogUMD,
            "Device {}: fw_range={}, asic_temperature={:.2f} C, board_temperature={} C",
            pci_device_id,
            fw_range_label(fw_version),
            asic_temp,
            board_temp.has_value() ? fmt::format("{:.2f}", board_temp.value()) : "nullopt");

        tt::ARCH arch = tt_device->get_arch();

        // Temperature should be in a sane range for a running chip.
        EXPECT_GT(asic_temp, 0.0);
        EXPECT_LT(asic_temp, 120.0);

        // BOARD_TEMPERATURE telemetry tag exists and can be read on all architectures.
        // On Blackhole it always returns 0 because SysEng hasn't wired up the actual I2C
        // board temp sensor yet.
        EXPECT_TRUE(board_temp.has_value());
        if (arch == tt::ARCH::BLACKHOLE) {
            EXPECT_DOUBLE_EQ(board_temp.value(), 0.0);
        } else if (board_temp.has_value()) {
            EXPECT_GT(board_temp.value(), 0.0);
            EXPECT_LT(board_temp.value(), 120.0);
        }
    }
}

TEST(TestFirmwareInfoProvider, ClockFrequencies) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        tt::ARCH arch = tt_device->get_arch();
        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();
        std::string range = fw_range_label(fw_version);
        uint32_t aiclk_busy_val = get_aiclk_busy_val(arch);

        std::optional<uint32_t> aiclk = fw_info->get_aiclk();
        std::optional<uint32_t> axiclk = fw_info->get_axiclk();
        std::optional<uint32_t> arcclk = fw_info->get_arcclk();
        uint32_t max_clock = fw_info->get_max_clock_freq();

        log_info(
            tt::LogUMD,
            "Device {}: arch={}, fw_range={}, aiclk={}, axiclk={}, arcclk={}, max_clock_freq={}, aiclk_busy_val={}",
            pci_device_id,
            arch_to_str(arch),
            range,
            aiclk.has_value() ? std::to_string(aiclk.value()) : "nullopt",
            axiclk.has_value() ? std::to_string(axiclk.value()) : "nullopt",
            arcclk.has_value() ? std::to_string(arcclk.value()) : "nullopt",
            max_clock,
            aiclk_busy_val);

        // Max clock frequency should match the architecture's AICLK_BUSY_VAL.
        EXPECT_EQ(max_clock, aiclk_busy_val);

        if (aiclk.has_value()) {
            EXPECT_GT(aiclk.value(), 0u);
            EXPECT_LE(aiclk.value(), aiclk_busy_val);
        }

        if (axiclk.has_value()) {
            EXPECT_GT(axiclk.value(), 0u);
            EXPECT_LE(axiclk.value(), aiclk_busy_val);
        }

        if (arcclk.has_value()) {
            EXPECT_GT(arcclk.value(), 0u);
            EXPECT_LE(arcclk.value(), aiclk_busy_val);
        }
    }
}

TEST(TestFirmwareInfoProvider, EthFirmwareVersion) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        tt::ARCH arch = tt_device->get_arch();
        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        std::optional<SemVer> eth_fw_version_semver = fw_info->get_eth_fw_version_semver();

        log_info(
            tt::LogUMD,
            "Device {}: arch={}, fw_range={}, eth_fw_version_semver={}",
            pci_device_id,
            arch_to_str(arch),
            fw_range_label(fw_version),
            eth_fw_version_semver.has_value() ? eth_fw_version_semver.value().to_string() : "nullopt");

        // Blackhole does not report ETH FW version via telemetry (NotAvailable across all FW ranges).
        if (arch == tt::ARCH::BLACKHOLE) {
            EXPECT_FALSE(eth_fw_version_semver.has_value());
        }

        // Wormhole should have ETH FW version available.
        if (arch == tt::ARCH::WORMHOLE_B0) {
            EXPECT_TRUE(eth_fw_version_semver.has_value());
        }
    }
}

TEST(TestFirmwareInfoProvider, SubcomponentFirmwareVersions) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        tt::ARCH arch = tt_device->get_arch();
        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        std::optional<SemVer> gddr_ver = fw_info->get_gddr_fw_version();
        std::optional<SemVer> cm_ver = fw_info->get_cm_fw_version();
        std::optional<SemVer> dm_app_ver = fw_info->get_dm_app_fw_version();
        std::optional<SemVer> dm_bl_ver = fw_info->get_dm_bl_fw_version();
        std::optional<SemVer> tt_flash_ver = fw_info->get_tt_flash_version();

        log_info(
            tt::LogUMD,
            "Device {}: arch={}, fw_range={}",
            pci_device_id,
            arch_to_str(arch),
            fw_range_label(fw_version));
        log_info(tt::LogUMD, "gddr_fw_version={}", gddr_ver.has_value() ? gddr_ver.value().to_string() : "nullopt");
        log_info(tt::LogUMD, "cm_fw_version={}", cm_ver.has_value() ? cm_ver.value().to_string() : "nullopt");
        log_info(
            tt::LogUMD, "dm_app_fw_version={}", dm_app_ver.has_value() ? dm_app_ver.value().to_string() : "nullopt");
        log_info(tt::LogUMD, "dm_bl_fw_version={}", dm_bl_ver.has_value() ? dm_bl_ver.value().to_string() : "nullopt");
        log_info(
            tt::LogUMD, "tt_flash_version={}", tt_flash_ver.has_value() ? tt_flash_ver.value().to_string() : "nullopt");

        // Legacy Wormhole (<= 18.3) does not have GDDR or CM firmware reporting.
        if (arch == tt::ARCH::WORMHOLE_B0 && fw_version <= FirmwareBundleVersion(18, 3, 0)) {
            EXPECT_FALSE(gddr_ver.has_value());
            EXPECT_FALSE(cm_ver.has_value());
        }
    }
}

TEST(TestFirmwareInfoProvider, PowerMetrics) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        std::optional<uint32_t> fan_speed = fw_info->get_fan_speed();
        std::optional<uint32_t> tdp = fw_info->get_tdp();
        std::optional<uint32_t> tdc = fw_info->get_tdc();
        std::optional<uint32_t> vcore = fw_info->get_vcore();

        tt::ARCH arch = tt_device->get_arch();

        log_info(
            tt::LogUMD,
            "Device {}: arch={}, fw_range={}",
            pci_device_id,
            arch_to_str(arch),
            fw_range_label(fw_version));
        log_info(
            tt::LogUMD,
            "fan_speed={} %",
            fan_speed.has_value() ? std::to_string(fan_speed.value()) : "nullopt (no fan / not controlled by FW)");
        log_info(tt::LogUMD, "tdp={} W", tdp.has_value() ? std::to_string(tdp.value()) : "nullopt");
        log_info(tt::LogUMD, "tdc={} A", tdc.has_value() ? std::to_string(tdc.value()) : "nullopt");
        log_info(tt::LogUMD, "vcore={} mV", vcore.has_value() ? std::to_string(vcore.value()) : "nullopt");

        // On legacy Blackhole (< 18.4), TDP and VCORE are not populated by firmware
        // and report 0.
        bool is_legacy_blackhole = arch == tt::ARCH::BLACKHOLE && fw_version < FirmwareBundleVersion(18, 4, 0);

        if (is_legacy_blackhole) {
            if (tdp.has_value()) {
                EXPECT_EQ(tdp.value(), 0u);
            }
            if (vcore.has_value()) {
                EXPECT_EQ(vcore.value(), 0u);
            }
        } else {
            if (tdp.has_value()) {
                EXPECT_LT(tdp.value(), 500u);
            }
            if (vcore.has_value()) {
                EXPECT_GT(vcore.value(), 0u);
                EXPECT_LT(vcore.value(), 2000u);
            }
        }
    }
}

TEST(TestFirmwareInfoProvider, DramTrainingStatus) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        tt::ARCH arch = tt_device->get_arch();
        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();
        uint32_t num_channels = get_num_dram_channels(arch);

        std::vector<DramTrainingStatus> statuses = fw_info->get_dram_training_status(num_channels);

        log_info(
            tt::LogUMD,
            "Device {}: arch={}, fw_range={}, num_dram_channels={}",
            pci_device_id,
            arch_to_str(arch),
            fw_range_label(fw_version),
            num_channels);

        // Wormhole has 6 DRAM channels, Blackhole has 8.
        EXPECT_EQ(statuses.size(), num_channels);
        if (arch == tt::ARCH::WORMHOLE_B0) {
            EXPECT_EQ(num_channels, 6u);
        } else if (arch == tt::ARCH::BLACKHOLE) {
            EXPECT_EQ(num_channels, 8u);
        }

        for (uint32_t ch = 0; ch < statuses.size(); ++ch) {
            log_info(tt::LogUMD, "DRAM channel {}: {}", ch, dram_training_status_to_str(statuses[ch]));
        }

        // IN_PROGRESS on Blackhole means the channel is either still training or has been harvested.
        // Note: Legacy WH (<= 18.3) uses 4-bit-per-channel format, modern uses 2-bit-per-channel.
        for (uint32_t ch = 0; ch < statuses.size(); ++ch) {
            EXPECT_TRUE(statuses[ch] == DramTrainingStatus::SUCCESS || statuses[ch] == DramTrainingStatus::IN_PROGRESS)
                << "DRAM channel " << ch << " reported FAIL";
        }
    }
}

TEST(TestFirmwareInfoProvider, AsicLocation) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        tt::ARCH arch = tt_device->get_arch();
        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        uint8_t asic_location = fw_info->get_asic_location();
        log_info(
            tt::LogUMD,
            "Device {}: arch={}, fw_range={}, asic_location={}",
            pci_device_id,
            arch_to_str(arch),
            fw_range_label(fw_version),
            static_cast<uint32_t>(asic_location));

        // Legacy Wormhole (<= 18.3) hardcodes ASIC_LOCATION to 0 (FixedValue).
        if (arch == tt::ARCH::WORMHOLE_B0 && fw_version <= FirmwareBundleVersion(18, 3, 0)) {
            EXPECT_EQ(asic_location, 0);
        }
    }
}

TEST(TestFirmwareInfoProvider, Heartbeat) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        FirmwareInfoProvider* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        // Read heartbeat twice and verify it doesn't decrease (monotonically non-decreasing).
        // Legacy WH reads from ARC0_HEALTH, modern reads from TIMER_HEARTBEAT.
        uint32_t heartbeat1 = fw_info->get_heartbeat();
        uint32_t heartbeat2 = fw_info->get_heartbeat();

        log_info(
            tt::LogUMD,
            "Device {}: fw_range={}, heartbeat_1={}, heartbeat_2={}",
            pci_device_id,
            fw_range_label(fw_version),
            heartbeat1,
            heartbeat2);

        EXPECT_GE(heartbeat2, heartbeat1);
    }
}

TEST(TestFirmwareInfoProvider, FanSpeed) {
    for (int id : PCIDevice::enumerate_devices()) {
        auto tt_device = TTDevice::create(id);
        tt_device->init_tt_device();
        auto* fw_info = tt_device->get_firmware_info_provider();
        ASSERT_NE(fw_info, nullptr);

        tt::ARCH arch = tt_device->get_arch();
        FirmwareBundleVersion fw_version = fw_info->get_firmware_version();

        auto speed_percentage = fw_info->get_fan_speed();
        auto speed_rpm = fw_info->get_fan_rpm();

        if (speed_percentage.has_value()) {
            EXPECT_GE(speed_percentage.value(), 0u);
            EXPECT_LE(speed_percentage.value(), 100u);
        }

        if (speed_rpm.has_value()) {
            EXPECT_LT(speed_rpm.value(), 20000u);
        }

        // FAN_RPM is not available in legacy Wormhole (<= 18.3) SMBus telemetry;
        // only FAN_SPEED (percentage) is reported.
        if (arch == tt::ARCH::WORMHOLE_B0 && fw_version <= FirmwareBundleVersion(18, 3, 0)) {
            EXPECT_FALSE(speed_rpm.has_value());
        } else {
            // On modern firmware, both should be available or both absent.
            EXPECT_EQ(speed_percentage.has_value(), speed_rpm.has_value());
        }
    }
}
