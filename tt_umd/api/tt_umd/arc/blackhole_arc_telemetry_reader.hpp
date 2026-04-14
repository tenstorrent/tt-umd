// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tt_umd/arc/arc_telemetry_reader.hpp"
#include "tt_umd/arch/blackhole_implementation.hpp"

namespace tt::umd {

class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(TTDevice* tt_device);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
