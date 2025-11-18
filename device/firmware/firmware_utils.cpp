/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/firmware/firmware_utils.hpp"

#include <cstdint>
#include <thread>

#include "tt-logger/tt-logger.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {
semver_t fw_version_from_telemetry(const uint32_t telemetry_data) {
    // The telemetry data is a 32-bit value where the higher 16 bits are the major value,
    // lower 16 bits are the minor value.
    uint16_t major = (telemetry_data >> 24) & 0xFF;
    uint16_t minor = (telemetry_data >> 16) & 0xFF;
    return semver_t(major, minor, 0);
}

semver_t get_firmware_version_util(TTDevice* tt_device) {
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        std::unique_ptr<SmBusArcTelemetryReader> smbus_telemetry_reader =
            std::make_unique<SmBusArcTelemetryReader>(tt_device);

        // Poll for a valid firmware version. If no valid version is found within 250ms,
        // log a warning and return the last read value.
        auto start = std::chrono::steady_clock::now();
        auto timeout_duration = std::chrono::milliseconds(250);
        while (std::chrono::steady_clock::now() - start < timeout_duration) {
            auto fw_bundle_version =
                smbus_telemetry_reader->read_entry(tt::umd::wormhole::TelemetryTag::FW_BUNDLE_VERSION);
            if (fw_bundle_version != 0) {
                return fw_version_from_telemetry(fw_bundle_version);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        log_warning(
            tt::LogUMD, "Timeout reading firmware bundle version (250ms), returning potentially invalid version");
        return fw_version_from_telemetry(
            smbus_telemetry_reader->read_entry(tt::umd::wormhole::TelemetryTag::FW_BUNDLE_VERSION));
    }
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)
               ? fw_version_from_telemetry(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION))
               : semver_t(0, 0, 0);
}

semver_t get_eth_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t(0, 0, 0);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_tt_flash_version_from_telemetry(const uint32_t telemetry_data) {
    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t(0, 0, 0);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t((telemetry_data >> 16) & 0xFFFF, telemetry_data & 0xFFFF, 0);
    }

    return semver_t(0, 0, 0);
}

}  // namespace tt::umd
