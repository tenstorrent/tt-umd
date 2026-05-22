// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

/**
 * @brief Abstract interface for reading telemetry entries from device firmware.
 *
 * Provides firmware-agnostic access to device telemetry. The telemetry tag
 * is a uint8_t identifier (max 255 entries) that the concrete implementation
 * maps to its firmware-specific table layout.
 *
 * Concrete implementations own the discovery of the telemetry table
 * address and the tag-to-offset mapping.
 */
class FirmwareTelemetryReader {
public:
    virtual ~FirmwareTelemetryReader() = default;

    /**
     * @brief Reads a telemetry entry by tag.
     *
     * @param tag Telemetry tag identifying the entry to read.
     * @return uint32_t The telemetry value.
     */
    virtual uint32_t read_entry(uint8_t tag) = 0;

    /**
     * @brief Checks whether a telemetry entry is available.
     *
     * @param tag Telemetry tag identifying the entry to check.
     * @return true if the entry exists in the firmware telemetry table.
     */
    virtual bool is_entry_available(uint8_t tag) = 0;
};

}  // namespace tt::umd
