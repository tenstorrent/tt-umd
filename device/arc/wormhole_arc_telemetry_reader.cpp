/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/arc/wormhole_arc_telemetry_reader.hpp"

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/telemetry.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

WormholeArcTelemetryReader::WormholeArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = !umd_use_noc1 ? tt::umd::wormhole::ARC_CORES_NOC0[0]
                             : tt_xy_pair(
                                   tt::umd::wormhole::NOC0_X_TO_NOC1_X[tt::umd::wormhole::ARC_CORES_NOC0[0].x],
                                   tt::umd::wormhole::NOC0_Y_TO_NOC1_Y[tt::umd::wormhole::ARC_CORES_NOC0[0].y]);
    get_telemetry_address();
    initialize_telemetry();
}

void WormholeArcTelemetryReader::get_telemetry_address() {
    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    uint32_t telemetry_table_addr_offset;
    tt_device->read_from_device(
        &telemetry_table_addr_offset,
        arc_core,
        tt::umd::wormhole::ARC_NOC_RESET_UNIT_BASE_ADDR + tt::umd::wormhole::NOC_NODEID_X_0,
        sizeof(uint32_t));

    telemetry_table_addr = telemetry_table_addr_offset + noc_telemetry_offset;

    uint32_t telemetry_values_addr_offset;
    tt_device->read_from_device(
        &telemetry_values_addr_offset,
        arc_core,
        tt::umd::wormhole::ARC_NOC_RESET_UNIT_BASE_ADDR + tt::umd::wormhole::NOC_NODEID_Y_0,
        sizeof(uint32_t));

    telemetry_values_addr = telemetry_values_addr_offset + noc_telemetry_offset;
}

}  // namespace tt::umd
