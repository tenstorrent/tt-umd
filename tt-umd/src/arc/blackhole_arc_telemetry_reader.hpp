// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tt-umd/arc/arc_telemetry_reader.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {
class DeviceProtocol;

/**
 * @brief Telemetry reader for Blackhole, whose ARC firmware publishes the telemetry table and its value array
 * through two reset-unit scratch registers.
 *
 * The device protocol is non-owning and must outlive this object.
 */
class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    // Waits for the firmware to publish the table, so a constructed reader always has entries to serve.
    BlackholeArcTelemetryReader(
        DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
