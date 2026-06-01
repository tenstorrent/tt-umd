// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/arc/arc_telemetry_reader.hpp"

namespace tt::umd {
class TTDevice;

class BlackholeArcTelemetryReader : public ArcTelemetryReader {
public:
    BlackholeArcTelemetryReader(TTDevice* tt_device);

protected:
    void get_telemetry_address() override;
};

}  // namespace tt::umd
