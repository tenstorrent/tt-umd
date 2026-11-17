// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/arc_telemetry_reader.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

ArcTelemetryReader::ArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    device_protocol(device_protocol), arc_core_noc0(arc_core_noc0), arc_core_noc1(arc_core_noc1) {}

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(
    DeviceProtocol* device_protocol,
    const tt::ARCH arch,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    UMD_THROW(error::RuntimeError, "ArcTelemetryReader::create_arc_telemetry_reader is not implemented yet");
}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "ArcTelemetryReader::read_entry is not implemented yet");
}

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "ArcTelemetryReader::is_entry_available is not implemented yet");
}

void ArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "ArcTelemetryReader::wait_for_telemetry_initialized is not implemented yet");
}

void ArcTelemetryReader::initialize_telemetry() {
    UMD_THROW(error::RuntimeError, "ArcTelemetryReader::initialize_telemetry is not implemented yet");
}

tt_xy_pair ArcTelemetryReader::get_arc_core(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1 : arc_core_noc0;
}

}  // namespace tt::umd
