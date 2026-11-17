// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/arc_telemetry_reader.hpp"

#include <fmt/format.h>

#include <chrono>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "arc/blackhole_arc_telemetry_reader.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Bound on the entry count of a sane table, guarding against reading a garbage header.
constexpr uint32_t MAX_TELEMETRY_ENTRIES = 1 << 16;

}  // namespace

ArcTelemetryReader::ArcTelemetryReader(
    DeviceProtocol* device_protocol, const tt_xy_pair arc_core_noc0, const tt_xy_pair arc_core_noc1) :
    device_protocol(device_protocol), arc_core_noc0(arc_core_noc0), arc_core_noc1(arc_core_noc1) {}

std::unique_ptr<ArcTelemetryReader> ArcTelemetryReader::create_arc_telemetry_reader(
    DeviceProtocol* device_protocol,
    const tt::ARCH arch,
    const tt_xy_pair arc_core_noc0,
    const tt_xy_pair arc_core_noc1,
    [[maybe_unused]] std::optional<uint64_t> legacy_telemetry_noc_addr) {
    std::unique_ptr<ArcTelemetryReader> reader;
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            reader = std::make_unique<BlackholeArcTelemetryReader>(device_protocol, arc_core_noc0, arc_core_noc1);
            break;
        default:
            UMD_THROW(error::RuntimeError, fmt::format("ARC telemetry is not supported on {}.", arch_to_str(arch)));
    }
    reader->wait_for_telemetry_initialized();
    return reader;
}

uint32_t ArcTelemetryReader::read_entry(const uint8_t telemetry_tag, NocId noc_id) {
    UMD_ASSERT(
        is_entry_available(telemetry_tag, noc_id),
        error::RuntimeError,
        fmt::format("Telemetry entry {} is not present in the firmware telemetry table.", telemetry_tag));

    // Static entries are served from the copy taken at init. A zero copy is read again: some of
    // them (the DMC versions) are filled in only once the DMC reports, which can follow init.
    const auto cached = telemetry_values.find(telemetry_tag);
    if (cached != telemetry_values.end() && cached->second != 0) {
        return cached->second;
    }

    uint32_t value = 0;
    device_protocol->read_ctrl(
        &value,
        get_arc_core(noc_id),
        telemetry_values_addr + telemetry_offset.at(telemetry_tag) * sizeof(uint32_t),
        sizeof(value),
        noc_id,
        RegisterAccessWidth::BITS32);
    return value;
}

bool ArcTelemetryReader::is_entry_available(const uint8_t telemetry_tag, [[maybe_unused]] NocId noc_id) {
    return telemetry_offset.count(telemetry_tag) != 0;
}

void ArcTelemetryReader::wait_for_telemetry_initialized(std::chrono::milliseconds timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout_ms;
    get_telemetry_address();
    while (telemetry_table_addr_reg == 0) {
        UMD_ASSERT(
            std::chrono::steady_clock::now() < deadline,
            error::RuntimeError,
            fmt::format(
                "ARC firmware did not publish its telemetry table within {} ms. Recovery firmware never publishes it.",
                timeout_ms.count()));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        get_telemetry_address();
    }
    initialize_telemetry();
}

void ArcTelemetryReader::initialize_telemetry() {
    const NocId noc_id = get_selected_noc_id();
    const tt_xy_pair arc_core = get_arc_core(noc_id);

    // Table header: version, then entry_count.
    uint32_t header[2] = {0, 0};
    device_protocol->read_ctrl(
        header, arc_core, telemetry_table_addr, sizeof(header), noc_id, RegisterAccessWidth::BITS32);
    entry_count = header[1];
    UMD_ASSERT(
        entry_count <= MAX_TELEMETRY_ENTRIES,
        error::RuntimeError,
        fmt::format("ARC telemetry table reports {} entries, which is not a valid table.", entry_count));
    log_debug(tt::LogUMD, "ARC telemetry table version {:#x} with {} entries.", header[0], entry_count);

    std::vector<TelemetryTagEntry> entries(entry_count);
    if (entry_count != 0) {
        device_protocol->read_ctrl(
            entries.data(),
            arc_core,
            telemetry_table_addr + sizeof(header),
            entry_count * sizeof(TelemetryTagEntry),
            noc_id,
            RegisterAccessWidth::BITS32);
    }

    telemetry_offset.clear();
    telemetry_values.clear();
    for (const TelemetryTagEntry& entry : entries) {
        // Slots the firmware leaves unused hold tag 0.
        if (entry.tag == 0) {
            continue;
        }
        telemetry_offset[entry.tag] = entry.offset;
    }

    for (const auto& [tag, offset] : telemetry_offset) {
        if (static_entries.count(tag) == 0) {
            continue;
        }
        uint32_t value = 0;
        device_protocol->read_ctrl(
            &value,
            arc_core,
            telemetry_values_addr + offset * sizeof(uint32_t),
            sizeof(value),
            noc_id,
            RegisterAccessWidth::BITS32);
        telemetry_values[tag] = value;
    }
}

tt_xy_pair ArcTelemetryReader::get_arc_core(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1 : arc_core_noc0;
}

}  // namespace tt::umd
