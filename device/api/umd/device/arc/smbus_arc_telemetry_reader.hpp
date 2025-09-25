/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <map>
#include <unordered_set>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_xy_pair.h"
#include "umd/device/types/telemetry.hpp"

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
