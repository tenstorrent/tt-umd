/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/arc_telemetry_reader.h"

#include "umd/device/blackhole_arc_telemetry_reader.h"
#include "umd/device/wormhole_arc_telemetry_reader.h"

namespace tt::umd {

ArcTelemetryReader::ArcTelemetryReader(TTDevice* tt_device) : tt_device(tt_device) {}

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(TTDevice* tt_device) {
    switch (tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeArcTelemetryReader>(tt_device);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeArcTelemetryReader>(tt_device);
        default:
            throw std::runtime_error("Unsupported architecture for creating Arc telemetry reader.");
    }
}

void ArcTelemetryReader::initialize_telemetry() {
    tt_device->read_from_device(&entry_count, arc_core, telemetry_table_addr + sizeof(uint32_t), sizeof(uint32_t));

    // We offset the tag_table_address by 2 * sizeof(uint32_t) to skip the first two uint32_t values,
    // which are version and entry count. For representaiton look at blackhole_telemetry.h
    uint32_t tag_table_address = telemetry_table_addr + 2 * sizeof(uint32_t);
    std::vector<TelemetryTagEntry> telemetry_tag_entries(entry_count);
    tt_device->read_from_device(
        telemetry_tag_entries.data(), arc_core, tag_table_address, entry_count * sizeof(TelemetryTagEntry));

    std::vector<uint32_t> telemetry_data(entry_count);
    tt_device->read_from_device(telemetry_data.data(), arc_core, telemetry_values_addr, entry_count * sizeof(uint32_t));

    for (const TelemetryTagEntry& tag_entry : telemetry_tag_entries) {
        const uint16_t tag_val = tag_entry.tag;
        const uint16_t offset_val = tag_entry.offset;

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
