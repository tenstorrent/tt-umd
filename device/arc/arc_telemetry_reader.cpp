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
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"
#include "utils.hpp"

namespace tt::umd {

static constexpr FirmwareBundleVersion FW_NEW_TELEMETRY = FirmwareBundleVersion(18, 4, 0);

ArcTelemetryReader::ArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    arc_core_noc0(arc_core_noc0), arc_core_noc1(arc_core_noc1), device_protocol(device_protocol) {}

tt_xy_pair ArcTelemetryReader::get_arc_core() const { return is_selected_noc1() ? arc_core_noc1 : arc_core_noc0; }

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(
    DeviceProtocol* device_protocol,
    const tt::ARCH arch,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    std::chrono::milliseconds timeout_ms) {
    std::unique_ptr<ArcTelemetryReader> reader;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            // The SMBus telemetry table is the only one guaranteed to exist on all Wormhole firmware versions, so
            // the bundle version deciding which reader to create has to be read through it.
            SmBusArcTelemetryReader smbus_reader(device_protocol, arc_core_noc0, arc_core_noc1);
            const FirmwareBundleVersion fw_bundle_version = FirmwareBundleVersion::from_firmware_bundle_tag(
                smbus_reader.read_entry(wormhole::LegacyTelemetryTag::FW_BUNDLE_VERSION));

            if (fw_bundle_version >= FW_NEW_TELEMETRY) {
                log_debug(tt::LogUMD, "Creating new-style telemetry reader.");
                reader = std::make_unique<WormholeArcTelemetryReader>(device_protocol, arc_core_noc0, arc_core_noc1);
            } else {
                log_debug(tt::LogUMD, "Creating old-style telemetry reader.");
                reader = std::make_unique<SmBusArcTelemetryReader>(device_protocol, arc_core_noc0, arc_core_noc1);
            }
            break;
        }
        case tt::ARCH::BLACKHOLE:
            log_debug(tt::LogUMD, "Creating new-style telemetry reader.");
            reader = std::make_unique<BlackholeArcTelemetryReader>(device_protocol, arc_core_noc0, arc_core_noc1);
            break;
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for creating ArcTelemetryReader.");
    }
    return reader;
}

void ArcTelemetryReader::initialize_telemetry() {
    device_protocol->read_data(
        &entry_count, get_arc_core(), telemetry_table_addr + sizeof(uint32_t), sizeof(uint32_t), get_selected_noc_id());

    // We offset the tag_table_address by 2 * sizeof(uint32_t) to skip the first two uint32_t values,
    // which are version and entry count. For representaiton look at telemetry.h
    uint64_t tag_table_address = telemetry_table_addr + 2 * sizeof(uint32_t);
    std::vector<TelemetryTagEntry> telemetry_tag_entries(entry_count);
    device_protocol->read_data(
        telemetry_tag_entries.data(),
        get_arc_core(),
        tag_table_address,
        entry_count * sizeof(TelemetryTagEntry),
        get_selected_noc_id());

    std::vector<uint32_t> telemetry_data(entry_count);
    device_protocol->read_data(
        telemetry_data.data(),
        get_arc_core(),
        telemetry_values_addr,
        entry_count * sizeof(uint32_t),
        get_selected_noc_id());

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t tag_offset;
        // + 8 is to skip first 2 numbers representing version and entry count.
        // 4 * i is to get to the i-th entry in the tag table where each entry is 4 bytes big.
        // Looking at layout in arc_telemetry_reader.h for reference.
        device_protocol->read_data(
            &tag_offset, get_arc_core(), telemetry_table_addr + 8 + 4 * i, sizeof(uint32_t), get_selected_noc_id());

        const uint16_t tag_val = tag_offset & 0xFFFF;
        const uint16_t offset_val = tag_offset >> 16;

        telemetry_values.insert({tag_val, telemetry_data[offset_val]});
        telemetry_offset.insert({tag_val, offset_val});
    }
}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) {
    if (!is_entry_available(telemetry_tag, noc_id)) {
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
    device_protocol->read_data(
        &telemetry_val,
        get_arc_core(),
        telemetry_values_addr + offset * sizeof(uint32_t),
        sizeof(uint32_t),
        get_selected_noc_id());

    telemetry_values[telemetry_tag] = telemetry_val;
    return telemetry_values[telemetry_tag];
}

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, NocId noc_id) {
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
