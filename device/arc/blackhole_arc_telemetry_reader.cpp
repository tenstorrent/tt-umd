/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"

#include <fmt/core.h>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/types/telemetry.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = blackhole::get_arc_core(tt_device->get_noc_translation_enabled(), umd_use_noc1);
    get_telemetry_address();
    initialize_telemetry();
}

void BlackholeArcTelemetryReader::get_telemetry_address() {
    uint32_t telemetry_table_addr_u32;
    tt_device->read_from_arc(&telemetry_table_addr_u32, tt::umd::blackhole::SCRATCH_RAM_13, sizeof(uint32_t));
    telemetry_table_addr = telemetry_table_addr_u32;
    uint32_t telemetry_values_addr_u32;
    tt_device->read_from_arc(&telemetry_values_addr_u32, tt::umd::blackhole::SCRATCH_RAM_12, sizeof(uint32_t));
    telemetry_values_addr = telemetry_values_addr_u32;
}

}  // namespace tt::umd
