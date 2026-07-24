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
#include "tt-logger/tt-logger.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SmBusArcTelemetryReader::SmBusArcTelemetryReader(TTDevice* tt_device) : ArcTelemetryReader(tt_device) {
    arc_core = !is_selected_noc1() ? wormhole::ARC_CORES_NOC0[0]
                                   : tt_xy_pair(
                                         wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                                         wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
    SmBusArcTelemetryReader::get_telemetry_address();
    SmBusArcTelemetryReader::wait_for_telemetry_initialized();
}

void SmBusArcTelemetryReader::get_telemetry_address() {
    std::vector<uint32_t> arc_msg_return_values = {0};
    tt_device->get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | (uint32_t)wormhole::arc_message_type::GET_SMBUS_TELEMETRY_ADDR,
        arc_msg_return_values,
        {0, 0});

    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    telemetry_base_noc_addr = arc_msg_return_values[0] + noc_telemetry_offset;
}

uint32_t SmBusArcTelemetryReader::read_entry(const uint8_t telemetry_tag) {
    if (!is_entry_available(telemetry_tag)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Telemetry entry {} not available. You can use is_entry_available() to check if the entry is "
                "available.",
                telemetry_tag));
    }

    uint32_t telemetry_value;
    tt_device->read_from_device(
        &telemetry_value,
        arc_core,
        telemetry_base_noc_addr + telemetry_tag * sizeof(uint32_t),
        sizeof(uint32_t),
        get_selected_noc_id());

    return telemetry_value;
}

bool SmBusArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag) {
    return telemetry_tag >= 0 && telemetry_tag < wormhole::LegacyTelemetryTag::NUMBER_OF_TAGS;
}

void SmBusArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    constexpr auto poll_interval = std::chrono::milliseconds(10);

    while (read_entry(wormhole::LegacyTelemetryTag::FW_BUNDLE_VERSION) == 0) {
        if (std::chrono::steady_clock::now() - start > timeout_ms) {
            log_warning(
                tt::LogUMD, "Timeout waiting for SMBus telemetry initialization (FW_BUNDLE_VERSION not populated).");
            return;
        }
        std::this_thread::sleep_for(poll_interval);
    }
}

}  // namespace tt::umd
