// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <unordered_set>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class DeviceProtocol;

class SmBusArcTelemetryReader : public ArcTelemetryReader {
public:
    // Where every Wormhole firmware that implements the legacy telemetry block has put it. Firmware
    // reports the location itself, over ARC message GET_SMBUS_TELEMETRY_ADDR, so this is only for
    // callers with no ARC message transport to ask through -- and for firmware that does not answer.
    static constexpr uint64_t DEFAULT_TELEMETRY_NOC_ADDR = 0x820078d60;

    // telemetry_base_noc_addr: base of the legacy telemetry block in NOC address space, as reported
    // by the firmware that published it.
    SmBusArcTelemetryReader(
        DeviceProtocol* device_protocol,
        const tt_xy_pair arc_core_noc0,
        const tt_xy_pair arc_core_noc1,
        uint64_t telemetry_base_noc_addr = DEFAULT_TELEMETRY_NOC_ADDR);

    uint32_t read_entry(const uint8_t telemetry_tag, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override;

    bool is_entry_available(const uint8_t telemetry_tag, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override;

protected:
    // Polls LegacyTelemetryTag::FW_BUNDLE_VERSION (the last entry written to the SMBus
    // flat telemetry array) until it becomes non-zero or timeout expires.
    // Uses the legacy tag index because TelemetryTag::FLASH_BUNDLE_VERSION=28 maps to a
    // different slot in the SMBus flat array.
    void wait_for_telemetry_initialized(
        std::chrono::milliseconds timeout_ms = timeout::TELEMETRY_INIT_TIMEOUT) override;

    void get_telemetry_address() override;

private:
    const uint64_t telemetry_base_noc_addr;
};

}  // namespace tt::umd
