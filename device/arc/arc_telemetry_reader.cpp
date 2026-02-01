// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/arc_telemetry_reader.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "tt-logger/tt-logger.hpp"
#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arc/wormhole_arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

static const tt::umd::semver_t new_telemetry_fw_bundle = tt::umd::semver_t(18, 4, 0);

namespace tt::umd {

ArcTelemetryReader::ArcTelemetryReader(TTDevice* tt_device) : tt_device(tt_device) {}

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(TTDevice* tt_device) {
    switch (tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            semver_t fw_bundle_version = get_firmware_version_util(tt_device);

            int compare_fw_bundles_result =
                semver_t::compare_firmware_bundle(fw_bundle_version, new_telemetry_fw_bundle);
            if (compare_fw_bundles_result >= 0) {
                log_debug(tt::LogUMD, "Creating new-style telemetry reader.");
                return std::make_unique<WormholeArcTelemetryReader>(tt_device);
            }

            log_debug(tt::LogUMD, "Creating old-style telemetry reader.");
            return std::make_unique<SmBusArcTelemetryReader>(tt_device);
        }
        case tt::ARCH::BLACKHOLE:
            log_debug(tt::LogUMD, "Creating new-style telemetry reader.");
            return std::make_unique<BlackholeArcTelemetryReader>(tt_device);
        default:
            throw std::runtime_error("Unsupported architecture for creating Arc telemetry reader.");
    }
}

void ArcTelemetryReader::initialize_telemetry() {
    tt_device->read_from_device(&entry_count, arc_core, telemetry_table_addr + sizeof(uint32_t), sizeof(uint32_t));

    // We offset the tag_table_address by 2 * sizeof(uint32_t) to skip the first two uint32_t values,
    // which are version and entry count. For representaiton look at telemetry.h
    uint32_t tag_table_address = telemetry_table_addr + 2 * sizeof(uint32_t);
    std::vector<TelemetryTagEntry> telemetry_tag_entries(entry_count);
    tt_device->read_from_device(
        telemetry_tag_entries.data(), arc_core, tag_table_address, entry_count * sizeof(TelemetryTagEntry));

    std::vector<uint32_t> telemetry_data(entry_count);
    tt_device->read_from_device(telemetry_data.data(), arc_core, telemetry_values_addr, entry_count * sizeof(uint32_t));

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t tag_offset;
        // + 8 is to skip first 2 numbers representing version and entry count.
        // 4 * i is to get to the i-th entry in the tag table where each entry is 4 bytes big.
        // Looking at layout in arc_telemetry_reader.h for reference.
        tt_device->read_from_device(&tag_offset, arc_core, telemetry_table_addr + 8 + 4 * i, sizeof(uint32_t));

        const uint16_t tag_val = tag_offset & 0xFFFF;
        const uint16_t offset_val = tag_offset >> 16;

        telemetry_values.insert({tag_val, telemetry_data[offset_val]});
        telemetry_offset.insert({tag_val, offset_val});
    }
}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag) {
    if (!is_entry_available(telemetry_tag)) {
        throw std::runtime_error(fmt::format(
            "Telemetry entry {} not available. You can use is_entry_available() to check if the entry is available.",
            telemetry_tag));
    }

    if (static_entries.find(telemetry_tag) != static_entries.end()) {
        return telemetry_values.at(telemetry_tag);
    }

    const uint32_t offset = telemetry_offset.at(telemetry_tag);
    uint32_t telemetry_val;
    tt_device->read_from_device(
        &telemetry_val, arc_core, telemetry_values_addr + offset * sizeof(uint32_t), sizeof(uint32_t));

    telemetry_values[telemetry_tag] = telemetry_val;
    return telemetry_values[telemetry_tag];
}

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag) {
    return telemetry_values.find(telemetry_tag) != telemetry_values.end();
}

}  // namespace tt::umd
