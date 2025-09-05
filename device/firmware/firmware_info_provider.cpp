// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/firmware/firmware_info_provider.hpp"

#include "umd/device/firmware/blackhole_18_7_firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp"
#include "umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/telemetry.hpp"

namespace tt::umd {

FirmwareInfoProvider::FirmwareInfoProvider(TTDevice* tt_device) :
    tt_device(tt_device), firmware_version(get_firmware_version_util(tt_device)) {}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProvider::create_firmware_info_provider(TTDevice* tt_device) {
    static const semver_t fw_version_18_7 = semver_t(18, 7, 0);
    static const semver_t fw_version_18_3 = semver_t(18, 3, 0);

    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0: {
            semver_t fw_bundle_version = get_firmware_version_util(tt_device);

            int compare_18_7_bundle_result = semver_t::compare_firmware_bundle(fw_bundle_version, fw_version_18_7);
            if (compare_18_7_bundle_result > 0) {
                return std::make_unique<FirmwareInfoProvider>(tt_device);
            }

            int compare_18_3_bundle_result = semver_t::compare_firmware_bundle(fw_bundle_version, fw_version_18_3);
            if (compare_18_3_bundle_result > 0) {
                return std::make_unique<Wormhole_18_7_FirmwareInfoProvider>(tt_device);
            }

            return std::make_unique<Wormhole_18_3_FirmwareInfoProvider>(tt_device);
        }
        case ARCH::BLACKHOLE: {
            semver_t fw_bundle_version = get_firmware_version_util(tt_device);

            int compare_18_7_bundle_result = semver_t::compare_firmware_bundle(fw_bundle_version, fw_version_18_7);
            if (compare_18_7_bundle_result > 0) {
                return std::make_unique<FirmwareInfoProvider>(tt_device);
            }

            return std::make_unique<Blackhole_18_7_FirmwareInfoProvider>(tt_device);
        }
        default:
            throw std::runtime_error("Unsupported architecture for firmware versioner.");
    }
}

semver_t FirmwareInfoProvider::get_firmware_version() { return firmware_version; }

semver_t FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            return semver_t(0, 0, 0);
        }
        case tt::ARCH::BLACKHOLE: {
            return semver_t(18, 5, 0);
        }
        default:
            throw std::runtime_error("Unsupported architecture for firmware info provider.");
    }
}

uint64_t FirmwareInfoProvider::get_board_id() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return (static_cast<uint64_t>(telemetry->read_entry(TelemetryTag::BOARD_ID_HIGH)) << 32) |
           (telemetry->read_entry(TelemetryTag::BOARD_ID_LOW));
}

uint32_t FirmwareInfoProvider::get_eth_fw_version() {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ETH_FW_VERSION);
}

double FirmwareInfoProvider::get_asic_temperature() {
    // Data stored in telemetry has temperature of ASIC stored in a way that high 16 bits
    // have integer part and lower 16 bits have fractional part.
    // It needs to be divided by 65536 to get temperature in Celsius.
    return static_cast<double>(tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_TEMPERATURE)) /
           65536.0f;
}

DramTrainingStatus FirmwareInfoProvider::get_dram_training_status(uint32_t dram_channel) {
    uint32_t telemetry_data = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::DDR_STATUS);
    if (telemetry_data & (1 << (2 * dram_channel))) {
        return DramTrainingStatus::SUCCESS;
    }

    if (telemetry_data & (1 << (2 * dram_channel + 1))) {
        return DramTrainingStatus::FAIL;
    }

    return DramTrainingStatus::IN_PROGRESS;
}

uint32_t FirmwareInfoProvider::get_max_clock_freq() {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::AICLK_LIMIT_MAX);
}

uint8_t FirmwareInfoProvider::get_asic_location() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::ASIC_LOCATION)
               ? static_cast<uint8_t>(telemetry->read_entry(TelemetryTag::ASIC_LOCATION))
               : 0;
}

}  // namespace tt::umd
