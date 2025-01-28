/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/blackhole_implementation.h"
#include "umd/device/tt_core_coordinates.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/blackhole_telemetry.h"

namespace tt::umd {

namespace blackhole {

class BlackholeArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(TTDevice* tt_device);

    uint32_t read_entry(const uint8_t telemetry_tag);

    bool is_entry_available(const uint8_t telemetry_tag);

private:
    void initialize_telemetry();

    TTDevice* tt_device;

    // Address of the telemetry table struct on ARC core.
    uint32_t telemetry_table_addr;

    // Number of entries in the telemetry table.
    uint32_t entry_count;

    // Address of the telemetry data on ARC core.
    uint32_t telemetry_values_addr;

    uint32_t telemetry_values[NUMBER_TELEMETRY_TAGS];
    bool telemetry_entry_available[NUMBER_TELEMETRY_TAGS];
    uint32_t telemetry_offset[NUMBER_TELEMETRY_TAGS];

    const tt_xy_pair arc_core = tt::umd::blackhole::ARC_CORES[0];
};

}  // namespace blackhole

}  // namespace tt::umd
