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

extern bool umd_use_noc1;

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

    // After entry_count the telemetry contains entry_count structs of TelemetryTagEntry.
    // Each struct contains tag and offset. Tag represents what is represented by the value.
    // Offset is the index of the telemetry value in the telemetry_values array.
    struct TelemetryTagEntry {
        uint16_t tag;
        uint16_t offset;
    };

    // Address of the telemetry data on ARC core.
    uint32_t telemetry_values_addr;

    std::map<uint32_t, uint32_t> telemetry_values;
    std::map<uint32_t, uint32_t> telemetry_offset;

    // During initialization of telemetry, if the NOC0 is hung then we need to read the telemetry values from NOC1.
    const tt_xy_pair arc_core =
        !umd_use_noc1 ? tt::umd::blackhole::ARC_CORES_NOC0[0]
                      : tt_xy_pair(
                            tt::umd::blackhole::NOC0_X_TO_NOC1_X[tt::umd::blackhole::ARC_CORES_NOC0[0].x],
                            tt::umd::blackhole::NOC0_Y_TO_NOC1_Y[tt::umd::blackhole::ARC_CORES_NOC0[0].y]);
};

}  // namespace blackhole

}  // namespace tt::umd
