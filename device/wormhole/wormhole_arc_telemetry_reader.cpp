/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/wormhole_arc_telemetry_reader.h"

#include "umd/device/types/wormhole_telemetry.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

WormholeArcTelemetryReader::WormholeArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = !umd_use_noc1 ? tt::umd::wormhole::ARC_CORES_NOC0[0]
                             : tt_xy_pair(
                                   tt::umd::wormhole::NOC0_X_TO_NOC1_X[tt::umd::wormhole::ARC_CORES_NOC0[0].x],
                                   tt::umd::wormhole::NOC0_Y_TO_NOC1_Y[tt::umd::wormhole::ARC_CORES_NOC0[0].y]);
    get_telemetry_address();
    initialize_telemetry();
    verify_telemetry();
}

void WormholeArcTelemetryReader::get_telemetry_address() {
    tt_device->read_from_device(
        &telemetry_table_addr, arc_core, tt::umd::wormhole::ARC_RESET_UNIT_BASE_ADDR + 0x1D0, sizeof(uint32_t));

    tt_device->read_from_device(
        &telemetry_values_addr, arc_core, tt::umd::wormhole::ARC_RESET_UNIT_BASE_ADDR + 0x1D4, sizeof(uint32_t));
}

void WormholeArcTelemetryReader::verify_telemetry() {
    // Seems that TAG_DEVICE_ID field in remote telemetry is not populated in the same way for remote and local chips.
    // TODO: figure out if there is any way for both local and remote chips to verify telemetry readouts.
    if (!tt_device->is_remote()) {
        uint32_t vendor_id = read_entry(tt::umd::wormhole::TAG_DEVICE_ID);
        constexpr uint32_t tt_vendor_id = 0x1e52;
        if ((vendor_id & 0xFFFF) != tt_vendor_id) {
            throw std::runtime_error(
                fmt::format("Tenstorrent vendor ID mismatch. Expected: 0x{:x}, Got: 0x{:x}", tt_vendor_id, vendor_id));
        }
    }
}

}  // namespace tt::umd
