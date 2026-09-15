// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/wormhole_arc_telemetry_reader.hpp"

#include <cstdint>
#include <vector>

#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

WormholeArcTelemetryReader::WormholeArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1) {
    wait_for_telemetry_initialized();
}

void WormholeArcTelemetryReader::get_telemetry_address() {
    uint32_t telemetry_table_arc_addr;
    device_protocol->read_data(
        &telemetry_table_arc_addr,
        get_arc_core(get_selected_noc_id()),
        wormhole::ARC_NOC_RESET_UNIT_BASE_ADDR + wormhole::NOC_NODEID_X_0,
        sizeof(uint32_t),
        get_selected_noc_id());

    telemetry_table_addr_reg = telemetry_table_arc_addr;
    telemetry_table_addr = telemetry_table_arc_addr + wormhole::ARC_NOC_ADDRESS_START;

    uint32_t telemetry_values_arc_addr;
    device_protocol->read_data(
        &telemetry_values_arc_addr,
        get_arc_core(get_selected_noc_id()),
        wormhole::ARC_NOC_RESET_UNIT_BASE_ADDR + wormhole::NOC_NODEID_Y_0,
        sizeof(uint32_t),
        get_selected_noc_id());

    telemetry_values_addr = telemetry_values_arc_addr + wormhole::ARC_NOC_ADDRESS_START;
}

}  // namespace tt::umd
