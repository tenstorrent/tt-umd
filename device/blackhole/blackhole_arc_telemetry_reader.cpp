/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/blackhole_arc_telemetry_reader.h"

#include <fmt/core.h>

#include "umd/device/blackhole_implementation.h"
#include "umd/device/types/telemetry.h"

extern bool umd_use_noc1;

namespace tt::umd {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = blackhole::get_arc_core(tt_device->get_noc_translation_enabled(), umd_use_noc1);
    get_telemetry_address();
    initialize_telemetry();
}

void BlackholeArcTelemetryReader::get_telemetry_address() {
    tt_device->read_from_device(&telemetry_table_addr, arc_core, tt::umd::blackhole::SCRATCH_RAM_13, sizeof(uint32_t));

    tt_device->read_from_device(&telemetry_values_addr, arc_core, tt::umd::blackhole::SCRATCH_RAM_12, sizeof(uint32_t));
}

}  // namespace tt::umd
