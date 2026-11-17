// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/smbus_arc_telemetry_reader.hpp"

#include <chrono>
#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

namespace {

constexpr const char* SMBUS_TELEMETRY = "SMBus telemetry (Wormhole firmware)";

}  // namespace

SmBusArcTelemetryReader::SmBusArcTelemetryReader(
    DeviceProtocol* device_protocol,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    uint64_t telemetry_base_noc_addr) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1),
    telemetry_base_noc_addr(telemetry_base_noc_addr) {
    UMD_NOT_SUPPORTED(SMBUS_TELEMETRY);
}

uint32_t SmBusArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) {
    UMD_NOT_SUPPORTED(SMBUS_TELEMETRY);
}

bool SmBusArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, NocId noc_id) {
    UMD_NOT_SUPPORTED(SMBUS_TELEMETRY);
}

void SmBusArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    UMD_NOT_SUPPORTED(SMBUS_TELEMETRY);
}

void SmBusArcTelemetryReader::get_telemetry_address() { UMD_NOT_SUPPORTED(SMBUS_TELEMETRY); }

}  // namespace tt::umd
