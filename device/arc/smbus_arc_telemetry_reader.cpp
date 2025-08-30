/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/arc/smbus_arc_telemetry_reader.h"

#include "umd/device/arch/wormhole_implementation.h"
#include "umd/device/types/wormhole_telemetry.h"

extern bool umd_use_noc1;

namespace tt::umd {

SmBusArcTelemetryReader::SmBusArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = !umd_use_noc1 ? tt::umd::wormhole::ARC_CORES_NOC0[0]
                             : tt_xy_pair(
                                   tt::umd::wormhole::NOC0_X_TO_NOC1_X[tt::umd::wormhole::ARC_CORES_NOC0[0].x],
                                   tt::umd::wormhole::NOC0_Y_TO_NOC1_Y[tt::umd::wormhole::ARC_CORES_NOC0[0].y]);
    get_telemetry_address();
}

void SmBusArcTelemetryReader::get_telemetry_address() {
    std::vector<uint32_t> arc_msg_return_values = {0};
    static const uint32_t timeout_ms = 1000;
    uint32_t exit_code = tt_device->get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | (uint32_t)wormhole::arc_message_type::GET_SMBUS_TELEMETRY_ADDR,
        arc_msg_return_values,
        0,
        0,
        timeout_ms);

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

}  // namespace tt::umd
