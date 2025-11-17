/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/firmware/firmware_utils.hpp"

#include <cstdint>
#include <optional>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

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

std::optional<semver_t> get_expected_eth_firmware_version_from_firmware_bundle(
    semver_t fw_bundle_version, tt::ARCH arch) {
    const auto* version_map = &erisc_firmware::WH_ERISC_FW_VERSION_MAP;
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            version_map = &erisc_firmware::WH_ERISC_FW_VERSION_MAP;
            break;
        case ARCH::BLACKHOLE:
            version_map = &erisc_firmware::BH_ERISC_FW_VERSION_MAP;
            break;
        default:
            return std::nullopt;
    }

    // Find the most recently updated ERISC FW version from a given firmware
    // bundle version.
    auto it = std::upper_bound(
        version_map->begin(),
        version_map->end(),
        fw_bundle_version,
        [](const semver_t& version, const std::pair<semver_t, semver_t>& entry) {
            // Use special comparison function that handles legacy FW bundle versions.
            return semver_t::compare_firmware_bundle(version, entry.first);
        });

    if (it != version_map->begin()) {
        --it;
        return it->second;
    }

    return std::nullopt;
}
}  // namespace tt::umd
