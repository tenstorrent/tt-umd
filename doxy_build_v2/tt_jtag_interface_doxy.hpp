// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

/**
 * @defgroup tt_jtag_interface JtagInterface
 * @{
 *
 * @brief JTAG-specific device access: memory-mapped register I/O.
 *
 * Exposes operations that are only meaningful for JTAG-connected devices.
 * Available from @ref TTDeviceModel when the active transport is JTAG.
 *
 * @optional
 *
 */

/**
 * @brief JTAG-specific device operations.
 */
class JtagInterface {
public:
    virtual ~JtagInterface() = default;

    /**
     * @brief Writes a 32-bit value to a memory mapped relative device address.
     * @param addr  Memory-mapped region relative address.
     * @param data The 32-bit value to write.
     */
    virtual void mmio_write32(uint32_t addr, uint32_t data) = 0;

    /**
     * @brief Reads a 32-bit value from a memory mapped relative device address.
     * @param addr Memory-mapped region relative address.
     * @return uint32_t The value read.
     */
    virtual uint32_t mmio_read32(uint32_t addr) = 0;
};

/** @} */  // end of tt_jtag_interface group

}  // namespace tt::umd
