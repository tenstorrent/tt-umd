/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/firmware/firmware_utils.h"

#include "umd/device/arc/smbus_arc_telemetry_reader.h"
#include "umd/device/types/telemetry.h"
#include "umd/device/types/wormhole_telemetry.h"

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

        return fw_version_from_telemetry(
            smbus_telemetry_reader->read_entry(tt::umd::wormhole::TelemetryTag::FW_BUNDLE_VERSION));
    }
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)
               ? fw_version_from_telemetry(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION))
               : semver_t(0, 0, 0);
}
}  // namespace tt::umd
