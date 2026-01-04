// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/wormhole_arc_telemetry_reader.hpp"

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/telemetry.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

WormholeArcTelemetryReader::WormholeArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    get_telemetry_address(umd_use_noc1);
    initialize_telemetry(umd_use_noc1);
}

tt_xy_pair WormholeArcTelemetryReader::get_arc_core(bool use_noc1) {
    return !use_noc1 ? wormhole::ARC_CORES_NOC0[0]
                     : tt_xy_pair(
                           wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                           wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
}

void WormholeArcTelemetryReader::get_telemetry_address(bool use_noc1) {
    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    auto arc_core = get_arc_core(use_noc1);
    uint32_t telemetry_table_addr_offset;
    tt_device->read_from_device(
        &telemetry_table_addr_offset,
        arc_core,
        wormhole::ARC_NOC_RESET_UNIT_BASE_ADDR + wormhole::NOC_NODEID_X_0,
        sizeof(uint32_t),
        umd_use_noc1);

    telemetry_table_addr = telemetry_table_addr_offset + noc_telemetry_offset;

    uint32_t telemetry_values_addr_offset;
    tt_device->read_from_device(
        &telemetry_values_addr_offset,
        arc_core,
        wormhole::ARC_NOC_RESET_UNIT_BASE_ADDR + wormhole::NOC_NODEID_Y_0,
        sizeof(uint32_t),
        umd_use_noc1);
    telemetry_values_addr = telemetry_values_addr_offset + noc_telemetry_offset;
}

}  // namespace tt::umd
