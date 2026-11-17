// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "arc/blackhole_arc_telemetry_reader.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

BlackholeArcTelemetryReader::BlackholeArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1) {}

void BlackholeArcTelemetryReader::get_telemetry_address() {
    UMD_THROW(error::RuntimeError, "BlackholeArcTelemetryReader::get_telemetry_address is not implemented yet");
}

}  // namespace tt::umd
