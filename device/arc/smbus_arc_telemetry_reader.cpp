// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"

#include <fmt/format.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

SmBusArcTelemetryReader::SmBusArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    ArcTelemetryReader(device_protocol, arc_core_noc0, arc_core_noc1) {
    SmBusArcTelemetryReader::get_telemetry_address();
    SmBusArcTelemetryReader::wait_for_telemetry_initialized();
}

void SmBusArcTelemetryReader::get_telemetry_address() {}

uint32_t SmBusArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) {
    if (!SmBusArcTelemetryReader::is_entry_available(telemetry_tag, noc_id)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Telemetry entry {} not available. You can use is_entry_available() to check if the entry is "
                "available.",
                telemetry_tag));
    }

    uint32_t telemetry_value;
    device_protocol->read_data(
        &telemetry_value,
        get_arc_core(get_selected_noc_id()),
        SMBUS_TELEMETRY_NOC_ADDR + telemetry_tag * sizeof(uint32_t),
        sizeof(uint32_t),
        get_selected_noc_id());

    return telemetry_value;
}

bool SmBusArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, NocId noc_id) {
    return telemetry_tag >= 0 && telemetry_tag < wormhole::LegacyTelemetryTag::NUMBER_OF_TAGS;
}

void SmBusArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    constexpr auto busy_poll_window = std::chrono::microseconds(10);
    constexpr auto poll_interval = std::chrono::milliseconds(10);

    const bool initialized = utils::poll_until(
        [this]() { return SmBusArcTelemetryReader::read_entry(wormhole::LegacyTelemetryTag::FW_BUNDLE_VERSION) != 0; },
        timeout_ms,
        busy_poll_window,
        poll_interval);

    if (!initialized) {
        UMD_THROW(
            error::RuntimeError,
            "Timeout waiting for SMBus telemetry initialization (FW_BUNDLE_VERSION not populated).");
    }
}

}  // namespace tt::umd
