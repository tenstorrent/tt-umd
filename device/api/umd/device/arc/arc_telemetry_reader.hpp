// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_set>

#include "firmware_telemetry_reader.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class DeviceProtocol;

class ArcTelemetryReader : public FirmwareTelemetryReader {
public:
    uint32_t read_entry(const uint8_t telemetry_tag, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override;

    bool is_entry_available(const uint8_t telemetry_tag, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override;

    // Constructs the reader and waits for the ARC telemetry table to be fully populated.
    static std::unique_ptr<ArcTelemetryReader> create_arc_telemetry_reader(
        DeviceProtocol* device_protocol,
        const tt::ARCH arch,
        const tt_xy_pair arc_core_noc0,
        const tt_xy_pair arc_core_noc1);

protected:
    ArcTelemetryReader(DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1);

    // Wait until ARC firmware has published the telemetry table. ARC writes the table pointer
    // register only after the whole table has been populated, so a non-zero pointer register
    // guarantees that all entries are present. Re-reads the pointer until it becomes non-zero or
    // the timeout expires, then initializes the table once.
    virtual void wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms = timeout::TELEMETRY_INIT_TIMEOUT);

    virtual void get_telemetry_address() = 0;

    void initialize_telemetry();

    // Address of the telemetry table struct on ARC core.
    uint64_t telemetry_table_addr{0};

    // Raw value of the ARC register holding the telemetry table pointer (0 until published).
    uint32_t telemetry_table_addr_reg{0};

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

    tt_xy_pair get_arc_core(NocId noc_id) const;

    DeviceProtocol* device_protocol;

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

    // During initialization of telemetry, if the NOC0 is hung then we need to read the telemetry values from NOC1.
    // Both sets of ARC core coordinates are kept, get_arc_core() picks the one for the selected NOC.
    tt_xy_pair arc_core_noc0;
    tt_xy_pair arc_core_noc1;
};

}  // namespace tt::umd
