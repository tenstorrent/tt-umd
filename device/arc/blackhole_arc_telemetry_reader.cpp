// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"

#include <fmt/core.h>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/types/telemetry.hpp"

namespace tt::umd {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(TTDevice* tt_device, bool use_noc1) :
    ArcTelemetryReader(tt_device) {
    get_telemetry_address(use_noc1);
    initialize_telemetry(use_noc1);
}

tt_xy_pair BlackholeArcTelemetryReader::get_arc_core(bool use_noc1) {
    return blackhole::get_arc_core(tt_device->get_noc_translation_enabled(), use_noc1);
}

void BlackholeArcTelemetryReader::get_telemetry_address(bool use_noc1) {
    uint32_t telemetry_table_addr_u32;
    tt_device->read_from_arc_apb(&telemetry_table_addr_u32, blackhole::SCRATCH_RAM_13, sizeof(uint32_t), use_noc1);
    telemetry_table_addr = telemetry_table_addr_u32;
    uint32_t telemetry_values_addr_u32;
    tt_device->read_from_arc_apb(&telemetry_values_addr_u32, blackhole::SCRATCH_RAM_12, sizeof(uint32_t), use_noc1);
    telemetry_values_addr = telemetry_values_addr_u32;
}

}  // namespace tt::umd
