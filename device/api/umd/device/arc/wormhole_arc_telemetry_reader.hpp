// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"

namespace tt::umd {
class DeviceProtocol;

class WormholeArcTelemetryReader : public ArcTelemetryReader {
public:
    WormholeArcTelemetryReader(
        DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
