/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/arc/arc_telemetry_reader.h"
#include "umd/device/arch/blackhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(TTDevice* tt_device);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
