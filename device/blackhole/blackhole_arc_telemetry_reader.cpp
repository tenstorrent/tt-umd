/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/blackhole_arc_telemetry_reader.h"

#include <fmt/core.h>

namespace tt::umd {

namespace blackhole {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(TTDevice* tt_device) : tt_device(tt_device) {
    initialize_telemetry();
}

void BlackholeArcTelemetryReader::initialize_telemetry() {
    for (uint8_t i = 0; i < NUMBER_TELEMETRY_TAGS; i++) {
        telemetry_entry_available[i] = false;
    }

    tt_device->read_from_device(
        &telemetry_table_addr,
        BlackholeArcTelemetryReader::arc_core,
        tt::umd::blackhole::SCRATCH_RAM_13,
        sizeof(uint32_t));

    tt_device->read_from_device(
        &entry_count, BlackholeArcTelemetryReader::arc_core, telemetry_table_addr + sizeof(uint32_t), sizeof(uint32_t));

    tt_device->read_from_device(
        &telemetry_values_addr,
        BlackholeArcTelemetryReader::arc_core,
        tt::umd::blackhole::SCRATCH_RAM_12,
        sizeof(uint32_t));

    // We offset the tag_table_address by 2 * sizeof(uint32_t) to skip the first two uint32_t values,
    // which are version and entry count. For representaiton look at blackhole_telemetry.h
    uint32_t tag_table_address = telemetry_table_addr + 2 * sizeof(uint32_t);
    std::vector<blackhole::telemetry_entry> telemetry_tag_entries(entry_count);
    tt_device->read_from_device(
        telemetry_tag_entries.data(),
        BlackholeArcTelemetryReader::arc_core,
        tag_table_address,
        entry_count * sizeof(blackhole::telemetry_entry));

    std::vector<uint32_t> telemetry_data(entry_count);
    tt_device->read_from_device(
        telemetry_data.data(),
        BlackholeArcTelemetryReader::arc_core,
        telemetry_values_addr,
        entry_count * sizeof(uint32_t));

    for (const blackhole::telemetry_entry& tag_entry : telemetry_tag_entries) {
        const uint16_t tag_val = tag_entry.tag;
        const uint16_t offset_val = tag_entry.offset;

        telemetry_values[tag_val - 1] = telemetry_data[offset_val];
        telemetry_entry_available[tag_val - 1] = true;
        telemetry_offset[tag_val - 1] = offset_val;
    }
}

uint32_t BlackholeArcTelemetryReader::read_entry(const uint8_t telemetry_tag) {
    if (telemetry_tag == 0 || telemetry_tag > NUMBER_TELEMETRY_TAGS) {
        throw std::runtime_error(fmt::format("Invalid telemtry tag {}", telemetry_tag));
    }

    if (!telemetry_entry_available[telemetry_tag - 1]) {
        throw std::runtime_error(fmt::format(
            "Telemetry entry {} not available. You can use is_entry_available() to check if the entry is available.",
            telemetry_tag));
    }

    const uint32_t offset = telemetry_offset[telemetry_tag - 1];
    uint32_t telemetry_val;
    tt_device->read_from_device(
        &telemetry_val,
        BlackholeArcTelemetryReader::arc_core,
        telemetry_values_addr + offset * sizeof(uint32_t),
        sizeof(uint32_t));

    telemetry_values[telemetry_tag - 1] = telemetry_val;
    return telemetry_values[telemetry_tag - 1];
}

bool BlackholeArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag) {
    return telemetry_entry_available[telemetry_tag - 1];
}

}  // namespace blackhole
}  // namespace tt::umd
