// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tt-umd/arc/arc_telemetry_reader.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"

namespace tt::umd {
class DeviceProtocol;

class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(
        DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
