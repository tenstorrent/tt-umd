/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/arc_telemetry_reader.h"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/types/blackhole_telemetry.h"

extern bool umd_use_noc1;

namespace tt::umd {

class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(TTDevice* tt_device);

    uint32_t read_entry(const uint8_t telemetry_tag) override;

    bool is_entry_available(const uint8_t telemetry_tag) override;

private:
    void initialize_telemetry();

    void read_static_telemetry_entries();

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
    const tt_xy_pair arc_core;

    bool static_telemetry_entries_initialized{false};
    std::unordered_map<uint8_t, uint32_t> static_telemetry_entries{
        {blackhole::TAG_BOARD_ID_HIGH, 0},
        {blackhole::TAG_BOARD_ID_LOW, 0},
        {blackhole::TAG_HARVESTING_STATE, 0},
        {blackhole::TAG_UPDATE_TELEM_SPEED, 0},
        {blackhole::TAG_ETH_FW_VERSION, 0},
        {blackhole::TAG_DDR_FW_VERSION, 0},
        {blackhole::TAG_BM_APP_FW_VERSION, 0},
        {blackhole::TAG_BM_BL_FW_VERSION, 0},
        {blackhole::TAG_FLASH_BUNDLE_VERSION, 0},
        {blackhole::TAG_CM_FW_VERSION, 0},
        {blackhole::TAG_L2CPU_FW_VERSION, 0},
        {blackhole::TAG_ENABLED_TENSIX_COL, 0},
        {blackhole::TAG_ENABLED_ETH, 0},
        {blackhole::TAG_ENABLED_GDDR, 0},
        {blackhole::TAG_ENABLED_L2CPU, 0},
        {blackhole::TAG_PCIE_USAGE, 0},
    };
};

}  // namespace tt::umd
