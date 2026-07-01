// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/arc_telemetry_reader.hpp"

#include <fmt/format.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "noc_access.hpp"
#include "tt-logger/tt-logger.hpp"
#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arc/wormhole_arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"
#include "utils.hpp"

namespace tt::umd {

static constexpr FirmwareBundleVersion FW_NEW_TELEMETRY = FirmwareBundleVersion(18, 4, 0);

ArcTelemetryReader::ArcTelemetryReader(TTDevice* tt_device) : tt_device(tt_device) {}

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(
    TTDevice* tt_device, std::chrono::milliseconds timeout_ms) {
    std::unique_ptr<ArcTelemetryReader> reader;
    switch (tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            FirmwareBundleVersion fw_bundle_version = get_firmware_version_util(tt_device);

            if (fw_bundle_version >= FW_NEW_TELEMETRY) {
                log_debug(tt::LogUMD, "Creating new-style telemetry reader.");
                reader = std::make_unique<WormholeArcTelemetryReader>(tt_device);
            } else {
                log_debug(tt::LogUMD, "Creating old-style telemetry reader.");
                reader = std::make_unique<SmBusArcTelemetryReader>(tt_device);
            }
            break;
        }
        case tt::ARCH::BLACKHOLE:
            log_debug(tt::LogUMD, "Creating new-style telemetry reader.");
            reader = std::make_unique<BlackholeArcTelemetryReader>(tt_device);
            break;
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for creating ArcTelemetryReader.");
    }
    reader->wait_for_telemetry_initialized(timeout_ms);
    return reader;
}

void ArcTelemetryReader::initialize_telemetry() {
    tt_device->read_from_device(
        &entry_count, arc_core, telemetry_table_addr + sizeof(uint32_t), sizeof(uint32_t), get_selected_noc_id());

    // We offset the tag_table_address by 2 * sizeof(uint32_t) to skip the first two uint32_t values,
    // which are version and entry count. For representaiton look at telemetry.h
    uint64_t tag_table_address = telemetry_table_addr + 2 * sizeof(uint32_t);
    std::vector<TelemetryTagEntry> telemetry_tag_entries(entry_count);
    tt_device->read_from_device(
        telemetry_tag_entries.data(),
        arc_core,
        tag_table_address,
        entry_count * sizeof(TelemetryTagEntry),
        get_selected_noc_id());

    std::vector<uint32_t> telemetry_data(entry_count);
    tt_device->read_from_device(
        telemetry_data.data(), arc_core, telemetry_values_addr, entry_count * sizeof(uint32_t), get_selected_noc_id());

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t tag_offset;
        // + 8 is to skip first 2 numbers representing version and entry count.
        // 4 * i is to get to the i-th entry in the tag table where each entry is 4 bytes big.
        // Looking at layout in arc_telemetry_reader.h for reference.
        tt_device->read_from_device(
            &tag_offset, arc_core, telemetry_table_addr + 8 + 4 * i, sizeof(uint32_t), get_selected_noc_id());

        const uint16_t tag_val = tag_offset & 0xFFFF;
        const uint16_t offset_val = tag_offset >> 16;

        telemetry_values.insert({tag_val, telemetry_data[offset_val]});
        telemetry_offset.insert({tag_val, offset_val});
    }
}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag) {
    if (!is_entry_available(telemetry_tag)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Telemetry entry {} not available. You can use is_entry_available() to check if the entry is "
                "available.",
                telemetry_tag));
    }

    if (static_entries.find(telemetry_tag) != static_entries.end()) {
        return telemetry_values.at(telemetry_tag);
    }

    const uint32_t offset = telemetry_offset.at(telemetry_tag);
    uint32_t telemetry_val;
    tt_device->read_from_device(
        &telemetry_val,
        arc_core,
        telemetry_values_addr + offset * sizeof(uint32_t),
        sizeof(uint32_t),
        get_selected_noc_id());

    telemetry_values[telemetry_tag] = telemetry_val;
    return telemetry_values[telemetry_tag];
}

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag) {
    return telemetry_values.find(telemetry_tag) != telemetry_values.end();
}

void ArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    constexpr auto busy_poll_window = std::chrono::microseconds(0);
    constexpr auto poll_interval = std::chrono::milliseconds(10);

    const bool initialized = utils::poll_until(
        [this]() {
            get_telemetry_address();
            return telemetry_table_addr_reg != 0;
        },
        timeout_ms,
        busy_poll_window,
        poll_interval);

    if (!initialized) {
        log_warning(tt::LogUMD, "Timeout waiting for ARC telemetry initialization (table pointer not published).");
        return;
    }

    initialize_telemetry();
}

}  // namespace tt::umd
