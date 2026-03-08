// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

SmBusArcTelemetryReader::SmBusArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = !is_selected_noc1() ? wormhole::ARC_CORES_NOC0[0]
                                   : tt_xy_pair(
                                         wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                         wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
    entry_count = wormhole::TelemetryTag::NUMBER_OF_TAGS;
    SmBusArcTelemetryReader::get_telemetry_address();
}

void SmBusArcTelemetryReader::get_telemetry_address() {
    std::vector<uint32_t> arc_msg_return_values = {0};
    tt_device->get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | (uint32_t)wormhole::arc_message_type::GET_SMBUS_TELEMETRY_ADDR,
        arc_msg_return_values,
        {0, 0});

    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    telemetry_base_noc_addr = arc_msg_return_values[0] + noc_telemetry_offset;
}

uint32_t SmBusArcTelemetryReader::read_entry(const uint8_t telemetry_tag) {
    if (!is_entry_available(telemetry_tag)) {
        throw std::runtime_error(fmt::format(
            "Telemetry entry {} not available. You can use is_entry_available() to check if the entry is available.",
            telemetry_tag));
    }

    uint32_t telemetry_value;
    tt_device->read_from_device(
        &telemetry_value, arc_core, telemetry_base_noc_addr + telemetry_tag * sizeof(uint32_t), sizeof(uint32_t));

    return telemetry_value;
}

bool SmBusArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag) {
    return telemetry_tag >= 0 && telemetry_tag < wormhole::TelemetryTag::NUMBER_OF_TAGS;
}

const std::map<uint32_t, std::optional<uint32_t>>& SmBusArcTelemetryReader::read_all_entries() {
    // SmBus tags are contiguous [0, NUMBER_OF_TAGS). Single bulk read of the entire table.
    static constexpr uint32_t num_tags = wormhole::TelemetryTag::NUMBER_OF_TAGS;
    bulk_read_buffer_.resize(num_tags);
    tt_device->read_from_device(
        bulk_read_buffer_.data(), arc_core, telemetry_base_noc_addr, num_tags * sizeof(uint32_t));

    // All tags in [0, NUMBER_OF_TAGS) are available.
    for (uint32_t tag = 0; tag < num_tags; ++tag) {
        bulk_read_cache_[tag] = bulk_read_buffer_[tag];
    }
    return bulk_read_cache_;
}

}  // namespace tt::umd
