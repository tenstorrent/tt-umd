// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/arc_telemetry_reader.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "arc/blackhole_arc_telemetry_reader.hpp"
#include "common/unsupported.hpp"
#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

ArcTelemetryReader::ArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    device_protocol(device_protocol), arc_core_noc0(arc_core_noc0), arc_core_noc1(arc_core_noc1) {}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) {
    if (!is_entry_available(telemetry_tag, noc_id)) {
        UMD_THROW(error::RuntimeError, fmt::format("Telemetry entry {} is not available.", telemetry_tag));
    }

    if (const auto static_value = telemetry_values.find(telemetry_tag); static_value != telemetry_values.end()) {
        return static_value->second;
    }

    uint32_t value = 0;
    device_protocol->read_ctrl(
        &value,
        get_arc_core(noc_id),
        telemetry_values_addr + telemetry_offset.at(telemetry_tag) * sizeof(uint32_t),
        sizeof(value),
        noc_id,
        RegisterAccessWidth::BITS32);
    return value;
}

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, NocId noc_id) {
    return telemetry_offset.find(telemetry_tag) != telemetry_offset.end();
}

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(
    DeviceProtocol* device_protocol,
    const tt::ARCH arch,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_NOT_SUPPORTED("ARC telemetry of " + arch_to_str(arch));
    }
    return std::make_unique<BlackholeArcTelemetryReader>(device_protocol, arc_core_noc0, arc_core_noc1);
}

void ArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    const bool published = utils::poll_until(
        [this]() {
            get_telemetry_address();
            return telemetry_table_addr_reg != 0;
        },
        timeout_ms,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL);
    if (!published) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("ARC firmware did not publish its telemetry table within {} ms.", timeout_ms.count()));
    }
    initialize_telemetry();
}

void ArcTelemetryReader::initialize_telemetry() {
    // Table layout: version, entry count, then one {tag, offset} pair per entry.
    const NocId noc_id = get_selected_noc_id();
    const tt_xy_pair arc_core = get_arc_core(noc_id);
    uint32_t header[2] = {};
    device_protocol->read_ctrl(
        header, arc_core, telemetry_table_addr, sizeof(header), noc_id, RegisterAccessWidth::BITS32);
    entry_count = header[1];

    std::vector<TelemetryTagEntry> entries(entry_count);
    if (entry_count != 0) {
        device_protocol->read_data(
            entries.data(),
            arc_core,
            telemetry_table_addr + sizeof(header),
            entries.size() * sizeof(TelemetryTagEntry),
            noc_id);
    }
    telemetry_offset.clear();
    telemetry_values.clear();
    for (const TelemetryTagEntry& entry : entries) {
        // Tag 0 marks an unused slot of the table.
        if (entry.tag == 0) {
            continue;
        }
        telemetry_offset[entry.tag] = entry.offset;
        if (static_entries.count(entry.tag) != 0) {
            uint32_t value = 0;
            device_protocol->read_ctrl(
                &value,
                arc_core,
                telemetry_values_addr + entry.offset * sizeof(uint32_t),
                sizeof(value),
                noc_id,
                RegisterAccessWidth::BITS32);
            telemetry_values[entry.tag] = value;
        }
    }
    log_debug(LogUMD, "ARC telemetry table at {:#x} has {} entries.", telemetry_table_addr, telemetry_offset.size());
}

tt_xy_pair ArcTelemetryReader::get_arc_core(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1 : arc_core_noc0;
}

}  // namespace tt::umd
