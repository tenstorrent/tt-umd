// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/arc_telemetry_reader.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "common/unsupported.hpp"

namespace tt::umd {

ArcTelemetryReader::ArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    device_protocol(device_protocol), arc_core_noc0(arc_core_noc0), arc_core_noc1(arc_core_noc1) {}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, NocId noc_id) { UMD_NOT_IMPLEMENTED(); }

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(
    DeviceProtocol* device_protocol,
    const tt::ARCH arch,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    UMD_NOT_IMPLEMENTED();
}

void ArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) { UMD_NOT_IMPLEMENTED(); }

void ArcTelemetryReader::initialize_telemetry() { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair ArcTelemetryReader::get_arc_core(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
