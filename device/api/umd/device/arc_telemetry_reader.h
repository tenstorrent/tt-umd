/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <map>
#include <unordered_set>

#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_xy_pair.h"
#include "umd/device/types/telemetry.h"

namespace tt::umd {

class ArcTelemetryReader {
public:
    virtual ~ArcTelemetryReader() = default;

    uint32_t read_entry(const uint8_t telemetry_tag);

    bool is_entry_available(const uint8_t telemetry_tag);

    static std::unique_ptr<ArcTelemetryReader> create_arc_telemetry_reader(TTDevice* tt_device);

protected:
    ArcTelemetryReader(TTDevice* tt_device);

    virtual void get_telemetry_address() = 0;

    void initialize_telemetry();

    // Address of the telemetry table struct on ARC core.
    uint64_t telemetry_table_addr;

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
    uint64_t telemetry_values_addr;

    std::map<uint32_t, uint32_t> telemetry_values;
    std::map<uint32_t, uint32_t> telemetry_offset;

    // During initialization of telemetry, if the NOC0 is hung then we need to read the telemetry values from NOC1.
    tt_xy_pair arc_core;

    TTDevice* tt_device;

private:
    const std::unordered_set<uint16_t> static_entries{
        TAG_BOARD_ID_HIGH,
        TAG_BOARD_ID_LOW,
        TAG_ASIC_ID,
        TAG_HARVESTING_STATE,
        TAG_UPDATE_TELEM_SPEED,
        TAG_ETH_FW_VERSION,
        TAG_DDR_FW_VERSION,
        TAG_BM_APP_FW_VERSION,
        TAG_BM_BL_FW_VERSION,
        TAG_FLASH_BUNDLE_VERSION,
        TAG_CM_FW_VERSION,
        TAG_L2CPU_FW_VERSION,
        TAG_ENABLED_TENSIX_COL,
        TAG_ENABLED_ETH,
        TAG_ENABLED_GDDR,
        TAG_ENABLED_L2CPU,
        TAG_PCIE_USAGE};
};

}  // namespace tt::umd
