// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"

namespace tt::umd {

class WormholeArcTelemetryReader : public ArcTelemetryReader {
public:
    WormholeArcTelemetryReader(TTDevice* tt_device);

    tt_xy_pair get_arc_core(bool use_noc1) override;

protected:
    void get_telemetry_address(bool use_noc1) override;
};

}  // namespace tt::umd
