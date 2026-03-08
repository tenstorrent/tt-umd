// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <optional>
#include <unordered_set>
#include <vector>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class ArcTelemetryReader {
public:
    virtual ~ArcTelemetryReader() = default;

    virtual uint32_t read_entry(const uint8_t telemetry_tag);

    virtual bool is_entry_available(const uint8_t telemetry_tag);

    uint32_t get_entry_count() const;

    /**
     * Read the entire telemetry values array in a single bulk read_from_device() call
     * and return a reference to the cached tag->value map. Does not modify the internal
     * telemetry_values cache used by read_entry(). Uses pre-allocated buffers so no
     * heap allocation occurs after initialization.
     * The returned reference is valid for the lifetime of this ArcTelemetryReader;
     * the values within the map are updated on each subsequent call.
     * Tags that are not available are set to std::nullopt.
     */
    virtual const std::map<uint32_t, std::optional<uint32_t>>& read_all_entries();

    static std::unique_ptr<ArcTelemetryReader> create_arc_telemetry_reader(TTDevice* tt_device);

protected:
    ArcTelemetryReader(TTDevice* tt_device);

    virtual void get_telemetry_address() = 0;

    void initialize_telemetry();

    // Address of the telemetry table struct on ARC core.
    uint64_t telemetry_table_addr{0};

    // Number of entries in the telemetry table.
    uint32_t entry_count{0};

    // After entry_count the telemetry contains entry_count structs of TelemetryTagEntry.
    // Each struct contains tag and offset. Tag represents what is represented by the value.
    // Offset is the index of the telemetry value in the telemetry_values array.
    struct TelemetryTagEntry {
        uint16_t tag;
        uint16_t offset;
    };

    // Address of the telemetry data on ARC core.
    uint64_t telemetry_values_addr{0};

    std::map<uint32_t, uint32_t> telemetry_values;
    std::map<uint32_t, uint32_t> telemetry_offset;

    // During initialization of telemetry, if the NOC0 is hung then we need to read the telemetry values from NOC1.
    tt_xy_pair arc_core;

    TTDevice* tt_device;

    // Pre-allocated buffers for read_all_entries() to avoid per-call allocation.
    std::vector<uint32_t> bulk_read_buffer_;
    std::map<uint32_t, std::optional<uint32_t>> bulk_read_cache_;

private:
    const std::unordered_set<uint16_t> static_entries{
        TelemetryTag::BOARD_ID_HIGH,
        TelemetryTag::BOARD_ID_LOW,
        TelemetryTag::ASIC_ID,
        TelemetryTag::HARVESTING_STATE,
        TelemetryTag::UPDATE_TELEM_SPEED,
        TelemetryTag::ETH_FW_VERSION,
        TelemetryTag::GDDR_FW_VERSION,
        TelemetryTag::DM_APP_FW_VERSION,
        TelemetryTag::DM_BL_FW_VERSION,
        TelemetryTag::FLASH_BUNDLE_VERSION,
        TelemetryTag::CM_FW_VERSION,
        TelemetryTag::L2CPU_FW_VERSION,
        TelemetryTag::ENABLED_TENSIX_COL,
        TelemetryTag::ENABLED_ETH,
        TelemetryTag::ENABLED_GDDR,
        TelemetryTag::ENABLED_L2CPU,
        TelemetryTag::PCIE_USAGE};
};

}  // namespace tt::umd
