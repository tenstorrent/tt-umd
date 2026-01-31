// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"
#include <cstdint>
#include <memory>

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

Wormhole_18_7_FirmwareInfoProvider::Wormhole_18_7_FirmwareInfoProvider(TTDevice* tt_device) :
    FirmwareInfoProvider(tt_device) {}

uint32_t Wormhole_18_7_FirmwareInfoProvider::get_max_clock_freq() const {
    const std::unique_ptr<SmBusArcTelemetryReader> sm_bus_telemetry =
        std::make_unique<SmBusArcTelemetryReader>(tt_device);
    uint32_t aiclk_telemetry = sm_bus_telemetry->read_entry(wormhole::AICLK);
    return (aiclk_telemetry >> 16) & 0xFFFF;
}

}  // namespace tt::umd
