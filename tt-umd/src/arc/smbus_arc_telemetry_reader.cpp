// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// SMBus telemetry only exists on Wormhole firmware, which this build does not support.

#include "tt-umd/arc/smbus_arc_telemetry_reader.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_smbus_telemetry_not_supported() {
    UMD_THROW(error::RuntimeError, "SMBus telemetry is only available on Wormhole, which is not supported.");
}

}  // namespace

SmBusArcTelemetryReader::SmBusArcTelemetryReader(
    DeviceProtocol* device_protocol,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    uint64_t telemetry_base_noc_addr) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1),
    telemetry_base_noc_addr(telemetry_base_noc_addr) {
    throw_smbus_telemetry_not_supported();
}

uint32_t SmBusArcTelemetryReader::read_entry(const uint8_t, NocId) { throw_smbus_telemetry_not_supported(); }

bool SmBusArcTelemetryReader::is_entry_available(const uint8_t, NocId) { throw_smbus_telemetry_not_supported(); }

void SmBusArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds) {
    throw_smbus_telemetry_not_supported();
}

void SmBusArcTelemetryReader::get_telemetry_address() { throw_smbus_telemetry_not_supported(); }

}  // namespace tt::umd
