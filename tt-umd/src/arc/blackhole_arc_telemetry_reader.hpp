// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tt-umd/arc/arc_telemetry_reader.hpp"

namespace tt::umd {

/**
 * @brief Telemetry reader for Blackhole firmware, which publishes where its telemetry table lives
 * in ARC scratch registers.
 */
class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(
        DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
