// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <unordered_set>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class SmBusArcTelemetryReader : public ArcTelemetryReader {
public:
    SmBusArcTelemetryReader(TTDevice* tt_device);

    uint32_t read_entry(const uint8_t telemetry_tag, bool use_noc1) override;

    bool is_entry_available(const uint8_t telemetry_tag) override;

    tt_xy_pair get_arc_core(bool use_noc1) override;

protected:
    void get_telemetry_address(bool use_noc1) override;

private:
    uint64_t telemetry_base_noc_addr;
};

}  // namespace tt::umd
