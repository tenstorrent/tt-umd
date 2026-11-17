// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "arc/blackhole_arc_telemetry_reader.hpp"

#include <cstdint>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/register_access_width.hpp"

namespace tt::umd {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1) {
    wait_for_telemetry_initialized();
}

void BlackholeArcTelemetryReader::get_telemetry_address() {
    const NocId noc_id = get_selected_noc_id();
    const auto read_scratch = [&](uint32_t scratch_offset) {
        uint32_t value = 0;
        device_protocol->read_ctrl(
            &value,
            get_arc_core(noc_id),
            blackhole::ARC_NOC_XBAR_ADDRESS_START + scratch_offset,
            sizeof(value),
            noc_id,
            RegisterAccessWidth::BITS32);
        return value;
    };
    // The firmware publishes ARC addresses of its CSM, which the ARC tile serves at the same NOC addresses.
    telemetry_table_addr_reg = read_scratch(blackhole::SCRATCH_RAM_13);
    telemetry_table_addr = telemetry_table_addr_reg;
    telemetry_values_addr = read_scratch(blackhole::SCRATCH_RAM_12);
}

}  // namespace tt::umd
