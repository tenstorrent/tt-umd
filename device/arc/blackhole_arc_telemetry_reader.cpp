// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"

#include <cstdint>

#include "noc_access.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"

namespace tt::umd {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1) {
    wait_for_telemetry_initialized();
}

void BlackholeArcTelemetryReader::get_telemetry_address() {
    // The ARC APB scratch RAM registers are reached over the NOC through the ARC XBAR window.
    uint32_t telemetry_table_addr_u32;
    device_protocol->read_ctrl(
        &telemetry_table_addr_u32,
        get_arc_core(get_selected_noc_id()),
        blackhole::ARC_NOC_XBAR_ADDRESS_START + blackhole::SCRATCH_RAM_13,
        sizeof(uint32_t),
        get_selected_noc_id());
    telemetry_table_addr_reg = telemetry_table_addr_u32;
    telemetry_table_addr = telemetry_table_addr_u32;
    uint32_t telemetry_values_addr_u32;
    device_protocol->read_ctrl(
        &telemetry_values_addr_u32,
        get_arc_core(get_selected_noc_id()),
        blackhole::ARC_NOC_XBAR_ADDRESS_START + blackhole::SCRATCH_RAM_12,
        sizeof(uint32_t),
        get_selected_noc_id());
    telemetry_values_addr = telemetry_values_addr_u32;
}

}  // namespace tt::umd
