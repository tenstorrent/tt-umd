// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

namespace tt::umd {

/**
 * @defgroup tt_hang_detector HangDetector
 * @{
 *
 * @brief Probes whether the device is reachable over the host bus or NOC.
 *
 * Reads a known register on a well-known tile. If the read returns all-ones
 * (HANG_READ_VALUE), the path is considered hung. Two independent paths:
 * - **Bus** — host-to-device link (PCIe, AXI, etc.).
 * - **NOC** — on-chip network routing.
 *
 * Returns `std::optional<bool>`: true = hung, false = healthy,
 * nullopt = check not applicable for this transport.
 *
 */

/**
 * @brief Detects device communication failures by probing known registers.
 */
class HangDetector {
public:
    virtual ~HangDetector() = default;

    /**
     * @brief Checks whether the host-to-device bus is hung.
     * @param data_read Value to pre-screen; skips device read if not the hang signature.
     * @return true = hung, false = healthy, nullopt = not supported.
     */
    std::optional<bool> is_bus_hung(uint32_t data_read = HANG_READ_VALUE);

    /**
     * @brief Checks whether NOC traffic to the device is hung.
     * @param noc Which NOC to probe.
     * @return true = hung, false = healthy, nullopt = not supported.
     */
    std::optional<bool> is_noc_hung(NocId noc);

protected:
    /// Reads the hang-check register via the host bus.
    virtual uint32_t read_hang_check_reg_via_bus() = 0;

    /// Reads the hang-check register via the specified NOC.
    virtual uint32_t read_hang_check_reg_via_noc(NocId noc) = 0;
};

/** @} */  // end of tt_hang_detector group

}  // namespace tt::umd
