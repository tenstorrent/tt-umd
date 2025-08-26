/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <map>
#include <unordered_set>

#include "umd/device/arc/arc_telemetry_reader.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_xy_pair.h"
#include "umd/device/types/telemetry.h"

namespace tt::umd {

class SmBusArcTelemetryReader : public ArcTelemetryReader {
public:
    SmBusArcTelemetryReader(TTDevice* tt_device);

    uint32_t read_entry(const uint8_t telemetry_tag) override;

    bool is_entry_available(const uint8_t telemetry_tag) override;

protected:
    void get_telemetry_address() override;

private:
    uint64_t telemetry_base_noc_addr;
};

}  // namespace tt::umd
