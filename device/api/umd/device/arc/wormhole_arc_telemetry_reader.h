/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/arc/arc_messenger.h"
#include "umd/device/arc/arc_telemetry_reader.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

class WormholeArcTelemetryReader : public ArcTelemetryReader {
public:
    WormholeArcTelemetryReader(TTDevice* tt_device);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
