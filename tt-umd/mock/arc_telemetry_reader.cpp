// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/arc_telemetry_reader.hpp"

namespace tt::umd {

class MockArcTelemetryReader : public ArcTelemetryReader {
public:
    MockArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {}

protected:
    void get_telemetry_address() override {}
};

ArcTelemetryReader::ArcTelemetryReader(TTDevice* tt_device) : tt_device(tt_device) {}

uint32_t ArcTelemetryReader::read_entry(const uint8_t) { return 0; }

bool ArcTelemetryReader::is_entry_available(const uint8_t) { return false; }

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(TTDevice* tt_device) {
    return std::make_unique<MockArcTelemetryReader>(tt_device);
}

}  // namespace tt::umd
